#define _POSIX_C_SOURCE 200809L
#include "process_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

typedef struct ProcessState {
    pid_t child;
    int stdin_write;
    int stdout_read;
    int stderr_read;
    pthread_t worker;
    pthread_mutex_t write_lock;
    pthread_mutex_t child_lock;
    int child_done;
    int child_status;
    int group_signaling_unsafe;
    int release_requested;
    int worker_finished;
    int cleanup_claimed;
} ProcessState;

static void process_destroy(AxyneProcess *process)
{
    ProcessState *state = (ProcessState *)process->implementation;
    (void)pthread_mutex_lock(&state->write_lock);
    close(state->stdin_write);
    (void)pthread_mutex_unlock(&state->write_lock);
    (void)pthread_mutex_destroy(&state->child_lock);
    (void)pthread_mutex_destroy(&state->write_lock);
    free(state);
    free(process);
}

static void process_try_deferred_destroy(AxyneProcess *process)
{
    ProcessState *state = (ProcessState *)process->implementation;
    int destroy = 0;
    (void)pthread_mutex_lock(&state->child_lock);
    if (state->release_requested && state->worker_finished &&
        !state->cleanup_claimed) {
        state->cleanup_claimed = 1;
        destroy = 1;
    }
    (void)pthread_mutex_unlock(&state->child_lock);
    if (destroy) process_destroy(process);
}

static unsigned char ascii_fold(unsigned char value)
{
    return value >= 'A' && value <= 'Z' ? (unsigned char)(value + ('a' - 'A')) : value;
}

static int same_environment_name(const char *left, size_t left_length,
                                const char *right, size_t right_length)
{
    size_t i;
    if (left_length != right_length) return 0;
    for (i = 0; i < left_length; ++i) {
        if (ascii_fold((unsigned char)left[i]) != ascii_fold((unsigned char)right[i]))
            return 0;
    }
    return 1;
}

static int set_cloexec(int fd)
{
    int flags = fcntl(fd, F_GETFD);
    return flags >= 0 && fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static char *find_executable(const char *executable)
{
    const char *path, *cursor;
    size_t name_length;
    if (strchr(executable, '/') != NULL) return strdup(executable);
    path = getenv("PATH");
    if (path == NULL) path = "/usr/bin:/bin";
    name_length = strlen(executable);
    cursor = path;
    for (;;) {
        const char *end = strchr(cursor, ':');
        size_t directory_length = end != NULL ? (size_t)(end - cursor) : strlen(cursor);
        size_t prefix_length = directory_length != 0 ? directory_length : 1;
        char *candidate = (char *)malloc(prefix_length + 1 + name_length + 1);
        if (candidate == NULL) return NULL;
        if (directory_length == 0) candidate[0] = '.';
        else memcpy(candidate, cursor, directory_length);
        candidate[prefix_length] = '/';
        memcpy(candidate + prefix_length + 1, executable, name_length + 1);
        if (access(candidate, X_OK) == 0) return candidate;
        free(candidate);
        if (end == NULL) break;
        cursor = end + 1;
    }
    return NULL;
}

static char **build_environment(const AxyneProcessSpec *spec)
{
    size_t inherited_count = 0, count, i, j;
    char **result;
    while (environ[inherited_count] != NULL) ++inherited_count;
    if (inherited_count > SIZE_MAX - spec->environment_count - 1) return NULL;
    result = (char **)calloc(inherited_count + spec->environment_count + 1,
                             sizeof(*result));
    if (result == NULL) return NULL;
    count = 0;
    for (i = 0; i < inherited_count; ++i) {
        const char *equals = strchr(environ[i], '=');
        size_t name_length = equals != NULL ? (size_t)(equals - environ[i]) : 0;
        int replaced = 0;
        for (j = 0; j < spec->environment_count; ++j) {
            const char *override = spec->environment[j];
            const char *override_equals = strchr(override, '=');
            if (override_equals != NULL &&
                same_environment_name(environ[i], name_length, override,
                                      (size_t)(override_equals - override))) {
                replaced = 1;
                break;
            }
        }
        if (!replaced) {
            result[count] = strdup(environ[i]);
            if (result[count] == NULL) goto fail;
            ++count;
        }
    }
    for (i = 0; i < spec->environment_count; ++i) {
        result[count] = strdup(spec->environment[i]);
        if (result[count] == NULL) goto fail;
        ++count;
    }
    return result;
fail:
    for (i = 0; i < count; ++i) free(result[i]);
    free(result);
    return NULL;
}

static void free_environment(char **environment)
{
    size_t i;
    if (environment == NULL) return;
    for (i = 0; environment[i] != NULL; ++i) free(environment[i]);
    free(environment);
}

static void consume_sigpipe_if_pending(const sigset_t *blocked)
{
    sigset_t pending;
    int received_signal;
    (void)sigpending(&pending);
    if (sigismember(&pending, SIGPIPE) == 1)
        (void)sigwait(blocked, &received_signal);
}

static void *process_worker(void *opaque)
{
    AxyneProcess *process = (AxyneProcess *)opaque;
    ProcessState *state = (ProcessState *)process->implementation;
    int descriptors[2] = { state->stdout_read, state->stderr_read };
    int eof[2] = { 0, 0 }, child_done = 0, pipe_error = 0;
    char buffer[4096];
    while (!child_done || !eof[0] || !eof[1]) {
        size_t stream;
        for (stream = 0; stream < 2; ++stream) {
            if (!eof[stream]) {
                for (;;) {
                    ssize_t amount = read(descriptors[stream], buffer, sizeof(buffer));
                    if (amount > 0) {
                        axyne_process_dispatch_output(process,
                            stream == 0 ? AXYNE_PROCESS_STDOUT : AXYNE_PROCESS_STDERR,
                            buffer, (size_t)amount);
                    } else if (amount == 0) {
                        eof[stream] = 1;
                        close(descriptors[stream]);
                    } else if (errno == EINTR) {
                        continue;
                    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        pipe_error = 1;
                        eof[stream] = 1;
                        close(descriptors[stream]);
                        (void)pthread_mutex_lock(&state->child_lock);
                        if (!state->group_signaling_unsafe &&
                            kill(-state->child, SIGKILL) != 0 && errno != ESRCH)
                            state->group_signaling_unsafe = 1;
                        (void)pthread_mutex_unlock(&state->child_lock);
                    }
                    break;
                }
            }
        }
        if (!child_done) {
            siginfo_t child_info;
            int wait_result;
            memset(&child_info, 0, sizeof(child_info));
            (void)pthread_mutex_lock(&state->child_lock);
            wait_result = waitid(P_PID, (id_t)state->child, &child_info,
                                 WEXITED | WNOHANG | WNOWAIT);
            if ((wait_result == 0 && child_info.si_pid == state->child) ||
                (wait_result < 0 && errno == ECHILD)) {
                child_done = 1;
                state->child_done = 1;
                if (wait_result == 0) {
                    state->child_status = child_info.si_status;
                    if (child_info.si_code == CLD_KILLED ||
                        child_info.si_code == CLD_DUMPED)
                        state->child_status = 128 + child_info.si_status;
                } else {
                    state->group_signaling_unsafe = 1;
                    state->child_status = 1;
                }
            } else if (wait_result < 0 && errno != EINTR) {
                child_done = 1;
                state->child_done = 1;
                state->group_signaling_unsafe = 1;
                state->child_status = 1;
            }
            (void)pthread_mutex_unlock(&state->child_lock);
        }
        if (!child_done || !eof[0] || !eof[1]) {
            struct timespec pause = { 0, 1000000L };
            (void)nanosleep(&pause, NULL);
        }
    }
    axyne_process_dispatch_exit(process, pipe_error ? -1 : state->child_status);
    (void)pthread_mutex_lock(&state->child_lock);
    state->worker_finished = 1;
    (void)pthread_mutex_unlock(&state->child_lock);
    process_try_deferred_destroy(process);
    return NULL;
}

AxyneStatus axyne_process_start(const AxyneProcessSpec *spec,
                                AxyneProcess **out, AxyneError *error)
{
    int input[2] = { -1, -1 }, output[2] = { -1, -1 }, errors[2] = { -1, -1 };
    int exec_error[2] = { -1, -1 }, child_errno = 0;
    ssize_t received;
    pid_t child;
    char *executable = NULL;
    char **environment = NULL;
    char **arguments = NULL;
    size_t i;
    AxyneProcess *process = NULL;
    ProcessState *state = NULL;
    AxyneStatus status;
    if (out != NULL) *out = NULL;
    if (out == NULL) return axyne_process_set_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                                                     "Output process pointer is null");
    status = axyne_process_validate_spec(spec, error);
    if (status != AXYNE_STATUS_OK) return status;
    executable = find_executable(spec->executable);
    if (executable == NULL) return axyne_process_set_error(error,
        errno == ENOMEM ? AXYNE_STATUS_OUT_OF_MEMORY : AXYNE_STATUS_NOT_FOUND,
        "Executable was not found on PATH");
    environment = build_environment(spec);
    arguments = (char **)calloc(spec->argument_count + 2, sizeof(*arguments));
    process = (AxyneProcess *)calloc(1, sizeof(*process));
    state = (ProcessState *)calloc(1, sizeof(*state));
    if (environment == NULL || arguments == NULL || process == NULL || state == NULL) {
        status = axyne_process_set_error(error, AXYNE_STATUS_OUT_OF_MEMORY,
                                         "Unable to allocate process state");
        goto cleanup;
    }
    arguments[0] = (char *)spec->executable;
    for (i = 0; i < spec->argument_count; ++i) arguments[i + 1] = (char *)spec->arguments[i];
    if (pipe(input) != 0 || pipe(output) != 0 || pipe(errors) != 0 || pipe(exec_error) != 0 ||
        !set_cloexec(exec_error[1])) {
        status = axyne_process_set_error(error, AXYNE_STATUS_IO_ERROR, "Unable to create process pipes");
        goto cleanup;
    }
    child = fork();
    if (child < 0) {
        status = axyne_process_set_error(error, AXYNE_STATUS_IO_ERROR, "Unable to fork child process");
        goto cleanup;
    }
    if (child == 0) {
        int saved_errno;
        if (setpgid(0, 0) != 0) {
            saved_errno = errno; (void)write(exec_error[1], &saved_errno, sizeof(saved_errno)); _exit(127);
        }
        close(input[1]); close(output[0]); close(errors[0]); close(exec_error[0]);
        if (dup2(input[0], STDIN_FILENO) < 0 || dup2(output[1], STDOUT_FILENO) < 0 ||
            dup2(errors[1], STDERR_FILENO) < 0 ||
            (spec->working_directory != NULL && chdir(spec->working_directory) != 0)) {
            saved_errno = errno; (void)write(exec_error[1], &saved_errno, sizeof(saved_errno)); _exit(127);
        }
        close(input[0]); close(output[1]); close(errors[1]);
        execve(executable, arguments, environment);
        saved_errno = errno; (void)write(exec_error[1], &saved_errno, sizeof(saved_errno)); _exit(127);
    }
    /* The child creates its group before exec; the parent closes the race
       before the process handle can be returned to the caller. */
    if (setpgid(child, child) != 0 &&
        !(errno == EACCES && getpgid(child) == child)) {
        (void)kill(child, SIGKILL); (void)waitpid(child, NULL, 0);
        status = axyne_process_set_error(error, AXYNE_STATUS_IO_ERROR,
                                         "Unable to create child process group");
        goto cleanup;
    }
    close(input[0]); input[0] = -1; close(output[1]); output[1] = -1;
    close(errors[1]); errors[1] = -1; close(exec_error[1]); exec_error[1] = -1;
    do { received = read(exec_error[0], &child_errno, sizeof(child_errno)); }
    while (received < 0 && errno == EINTR);
    close(exec_error[0]); exec_error[0] = -1;
    if (received > 0) {
        (void)waitpid(child, NULL, 0);
        status = axyne_process_set_error(error,
            child_errno == EACCES ? AXYNE_STATUS_PERMISSION_DENIED : AXYNE_STATUS_IO_ERROR,
            "Unable to execute child process");
        goto cleanup;
    }
    if (!set_nonblocking(output[0]) || !set_nonblocking(errors[0])) {
        (void)kill(-child, SIGKILL); (void)waitpid(child, NULL, 0);
        status = axyne_process_set_error(error, AXYNE_STATUS_IO_ERROR,
                                         "Unable to configure process output pipes");
        goto cleanup;
    }
    state->child = child; state->stdin_write = input[1]; input[1] = -1;
    state->stdout_read = output[0]; output[0] = -1;
    state->stderr_read = errors[0]; errors[0] = -1;
    if (pthread_mutex_init(&state->write_lock, NULL) != 0) {
        (void)kill(-child, SIGKILL); (void)waitpid(child, NULL, 0);
        close(state->stdin_write); close(state->stdout_read); close(state->stderr_read);
        state->stdin_write = state->stdout_read = state->stderr_read = -1;
        status = axyne_process_set_error(error, AXYNE_STATUS_IO_ERROR,
                                         "Unable to initialize process synchronization");
        goto cleanup;
    }
    if (pthread_mutex_init(&state->child_lock, NULL) != 0) {
        (void)kill(-child, SIGKILL); (void)waitpid(child, NULL, 0);
        pthread_mutex_destroy(&state->write_lock);
        close(state->stdin_write); close(state->stdout_read); close(state->stderr_read);
        state->stdin_write = state->stdout_read = state->stderr_read = -1;
        status = axyne_process_set_error(error, AXYNE_STATUS_IO_ERROR,
                                         "Unable to initialize process synchronization");
        goto cleanup;
    }
    process->implementation = state; process->on_output = spec->on_output;
    process->on_exit = spec->on_exit; process->user_data = spec->user_data;
    if (pthread_create(&state->worker, NULL, process_worker, process) != 0) {
        (void)kill(-child, SIGKILL); (void)waitpid(child, NULL, 0);
        pthread_mutex_destroy(&state->child_lock);
        pthread_mutex_destroy(&state->write_lock);
        close(state->stdin_write); close(state->stdout_read); close(state->stderr_read);
        state->stdin_write = state->stdout_read = state->stderr_read = -1;
        status = axyne_process_set_error(error, AXYNE_STATUS_IO_ERROR,
                                         "Unable to start process output worker");
        goto cleanup;
    }
    *out = process;
    free(executable); free(arguments); free_environment(environment);
    return axyne_process_set_error(error, AXYNE_STATUS_OK, "");
cleanup:
    if (input[0] >= 0) close(input[0]); if (input[1] >= 0) close(input[1]);
    if (output[0] >= 0) close(output[0]); if (output[1] >= 0) close(output[1]);
    if (errors[0] >= 0) close(errors[0]); if (errors[1] >= 0) close(errors[1]);
    if (exec_error[0] >= 0) close(exec_error[0]); if (exec_error[1] >= 0) close(exec_error[1]);
    free(executable); free(arguments); free_environment(environment);
    free(state); free(process);
    return status;
}

AxyneStatus axyne_process_write(AxyneProcess *process, const char *bytes,
                                size_t length, AxyneError *error)
{
    ProcessState *state;
    size_t offset = 0;
    sigset_t blocked, old_mask, pending;
    int had_pending = 0;
    if (process == NULL || (length != 0 && bytes == NULL))
        return axyne_process_set_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "Invalid process write");
    state = (ProcessState *)process->implementation;
    (void)pthread_mutex_lock(&state->write_lock);
    sigemptyset(&blocked); sigaddset(&blocked, SIGPIPE);
    (void)pthread_sigmask(SIG_BLOCK, &blocked, &old_mask);
    (void)sigpending(&pending); had_pending = sigismember(&pending, SIGPIPE) == 1;
    while (offset < length) {
        size_t remaining = length - offset;
        size_t amount = remaining > (size_t)SSIZE_MAX ? (size_t)SSIZE_MAX : remaining;
        ssize_t written = write(state->stdin_write, bytes + offset, amount);
        if (written > 0) offset += (size_t)written;
        else if (written < 0 && errno == EINTR) continue;
        else {
            if (errno == EPIPE && !had_pending) {
                consume_sigpipe_if_pending(&blocked);
            }
            (void)pthread_sigmask(SIG_SETMASK, &old_mask, NULL);
            (void)pthread_mutex_unlock(&state->write_lock);
            return axyne_process_set_error(error, AXYNE_STATUS_IO_ERROR,
                                           "Unable to write to child standard input");
        }
    }
    (void)pthread_sigmask(SIG_SETMASK, &old_mask, NULL);
    (void)pthread_mutex_unlock(&state->write_lock);
    return axyne_process_set_error(error, AXYNE_STATUS_OK, "");
}

AxyneStatus axyne_process_terminate(AxyneProcess *process, AxyneError *error)
{
    ProcessState *state;
    if (process == NULL) return axyne_process_set_error(error, AXYNE_STATUS_INVALID_ARGUMENT,
                                                        "Process is null");
    state = (ProcessState *)process->implementation;
    (void)pthread_mutex_lock(&state->child_lock);
    if (!state->group_signaling_unsafe &&
        kill(-state->child, SIGKILL) != 0 && errno != ESRCH) {
        (void)pthread_mutex_unlock(&state->child_lock);
        return axyne_process_set_error(error, AXYNE_STATUS_IO_ERROR,
                                       "Unable to terminate child process group");
    }
    (void)pthread_mutex_unlock(&state->child_lock);
    return axyne_process_set_error(error, AXYNE_STATUS_OK, "");
}

void axyne_process_release(AxyneProcess *process)
{
    ProcessState *state;
    int destroy = 0;
    if (process == NULL) return;
    state = (ProcessState *)process->implementation;
    if (pthread_equal(pthread_self(), state->worker)) {
        (void)pthread_mutex_lock(&state->child_lock);
        state->release_requested = 1;
        (void)pthread_mutex_unlock(&state->child_lock);
        return;
    }
    (void)pthread_mutex_lock(&state->child_lock);
    if (!state->group_signaling_unsafe) (void)kill(-state->child, SIGKILL);
    (void)pthread_mutex_unlock(&state->child_lock);
    (void)pthread_join(state->worker, NULL);
    /* Normally the zombie leader reserves its process-group ID through the
       final group signal. If a host SIGCHLD handler reaped it, do not signal
       the potentially reused ID; waitpid below remains best-effort cleanup. */
    while (waitpid(state->child, NULL, 0) < 0 && errno == EINTR) { }
    (void)pthread_mutex_lock(&state->child_lock);
    if (!state->cleanup_claimed) {
        state->cleanup_claimed = 1;
        destroy = 1;
    }
    (void)pthread_mutex_unlock(&state->child_lock);
    if (destroy) process_destroy(process);
}

void axyne_process_release_deferred(AxyneProcess *process)
{
    ProcessState *state;
    if (process == NULL) return;
    state = (ProcessState *)process->implementation;
    (void)pthread_mutex_lock(&state->child_lock);
    state->release_requested = 1;
    (void)pthread_mutex_unlock(&state->child_lock);
    process_try_deferred_destroy(process);
}
