#include "terminal_internal.hpp"
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

extern char **environ;
struct PosixTerminal {
    std::mutex child_mutex;
    int master = -1;
    pid_t child = -1; // Kept unreaped until session teardown; cannot be reused.
    bool child_exited = false;
};
static void nonblocking(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK); }
static void cloexec(int fd) { fcntl(fd, F_SETFD, FD_CLOEXEC); }
static AxyneStatus failure(AxyneError *e, const char *what) {
    std::string message = std::string(what) + ": " + std::strerror(errno);
    return terminal_result(e, AXYNE_STATUS_IO_ERROR, message.c_str());
}
static void writer(AxyneTerminal *t, PosixTerminal *p) {
    // Ignore SIGPIPE on this worker only; do not change the application's policy.
    sigset_t set; sigemptyset(&set); sigaddset(&set, SIGPIPE); pthread_sigmask(SIG_BLOCK, &set, nullptr);
    while (!t->stopping) {
        std::string data;
        {
            std::unique_lock<std::mutex> lock(t->queue_mutex);
            t->queue_ready.wait(lock, [&]{ return t->stopping || !t->writes.empty(); });
            if (t->stopping) break;
            data = std::move(t->writes.front()); t->writes.pop_front();
        }
        size_t at = 0;
        while (at < data.size() && !t->stopping) {
            pollfd fd{p->master, POLLOUT, 0};
            int ready = poll(&fd, 1, 50);
            if (ready < 0 && errno == EINTR) continue;
            if (ready < 0 || (fd.revents & (POLLHUP | POLLERR | POLLNVAL))) break;
            if (!ready) continue;
            ssize_t n = write(p->master, data.data() + at, data.size() - at);
            if (n > 0) at += static_cast<size_t>(n);
            else if (n < 0 && errno != EAGAIN && errno != EINTR) break;
        }
        { std::lock_guard<std::mutex> lock(t->queue_mutex); t->queued_bytes -= data.size(); }
    }
}
static void reader(AxyneTerminal *t, PosixTerminal *p) {
    using Clock = std::chrono::steady_clock;
    auto end = Clock::time_point::max();
    bool eof = false;
    int code = -1;
    while (!eof) {
        // WNOWAIT deliberately retains PID/session ownership until stop and join.
        siginfo_t info{};
        { std::lock_guard<std::mutex> lock(p->child_mutex);
          if (!p->child_exited && waitid(P_PID, p->child, &info, WEXITED | WNOHANG | WNOWAIT) == 0 && info.si_pid) {
              p->child_exited = true;
              code = info.si_code == CLD_EXITED ? info.si_status : 128 + info.si_status;
              end = Clock::now() + std::chrono::milliseconds(500);
          }
        }
        if (t->stopping && end == Clock::time_point::max()) end = Clock::now() + std::chrono::milliseconds(500);
        pollfd fd{p->master, POLLIN, 0};
        int ready = poll(&fd, 1, 50);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) break;
        if (ready && (fd.revents & (POLLIN | POLLHUP | POLLERR))) {
            char buffer[16384];
            // Bounded per turn even when a child produces output continuously.
            for (int batch = 0; batch < 16; ++batch) {
                ssize_t n = read(p->master, buffer, sizeof buffer);
                if (n > 0) {
                    axyne_terminal_feed(t, buffer, static_cast<size_t>(n));
                    if (t->changed) t->changed(t, t->context);
                } else {
                    if (n == 0 || (errno != EINTR && errno != EAGAIN)) eof = true;
                    break;
                }
            }
        }
        if (Clock::now() >= end) break;
    }
    t->running = false; t->stopping = true; t->queue_ready.notify_all();
    // EOF may race the first WNOHANG check. Observe the root after draining,
    // retaining its zombie rather than reaping the session/group identity.
    if (code == -1) {
        siginfo_t info{};
        auto status_deadline = Clock::now() + std::chrono::milliseconds(500);
        for (;;) {
            { std::lock_guard<std::mutex> lock(p->child_mutex);
              int result = waitid(P_PID, p->child, &info, WEXITED | WNOHANG | WNOWAIT);
              if (result < 0 || info.si_pid) {
                  p->child_exited = true;
                  if (info.si_pid) code = info.si_code == CLD_EXITED ? info.si_status : 128 + info.si_status;
                  break;
              }
            }
            if (Clock::now() >= status_deadline) terminal_transport_stop(t);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    if (t->exited) t->exited(t, code, t->context);
}
AxyneStatus terminal_transport_start(AxyneTerminal *t, const AxyneTerminalSpec *s, AxyneError *e) {
    auto *p = new (std::nothrow) PosixTerminal;
    if (!p) return terminal_result(e, AXYNE_STATUS_OUT_OF_MEMORY);
    t->transport = p; // Exception cleanup owns every opened descriptor.
    p->master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (p->master < 0 || grantpt(p->master) || unlockpt(p->master)) {
        auto result = failure(e, "Cannot allocate PTY"); terminal_transport_destroy(t); return result;
    }
    // All strings, argv, and environment are constructed before fork.
    std::string slave_name = ptsname(p->master);
    std::vector<std::string> env;
    for (char **entry = environ; *entry; ++entry) {
        if (std::strncmp(*entry, "TERM=", 5) && std::strncmp(*entry, "COLORTERM=", 10)) env.emplace_back(*entry);
    }
    env.emplace_back("TERM=xterm-256color"); env.emplace_back("COLORTERM=truecolor");
    std::vector<char *> envp; for (auto &entry : env) envp.push_back(entry.data()); envp.push_back(nullptr);
    std::vector<std::string> args; args.emplace_back(s->executable);
    for (size_t i = 0; i < s->argument_count; ++i) {
        if (!s->arguments[i]) { terminal_transport_destroy(t); return terminal_result(e, AXYNE_STATUS_INVALID_ARGUMENT); }
        args.emplace_back(s->arguments[i]);
    }
    std::vector<char *> argv; for (auto &arg : args) argv.push_back(arg.data()); argv.push_back(nullptr);
    std::string cwd = s->working_directory ? s->working_directory : "";
    int handshake[2];
    if (pipe(handshake)) { auto result = failure(e, "Cannot create startup handshake"); terminal_transport_destroy(t); return result; }
    cloexec(handshake[0]); cloexec(handshake[1]);
    int rows, cols; { std::lock_guard<std::mutex> lock(t->model_mutex); vterm_get_size(t->vt, &rows, &cols); }
    winsize size{}; size.ws_row = static_cast<unsigned short>(rows); size.ws_col = static_cast<unsigned short>(cols);
    p->child = fork();
    if (!p->child) {
        close(handshake[0]);
        int saved = 0;
        if (setsid() < 0) saved = errno;
        int slave = saved ? -1 : open(slave_name.c_str(), O_RDWR);
        if (slave < 0 && !saved) saved = errno;
        if (!saved && ioctl(slave, TIOCSCTTY, 0) < 0) saved = errno;
        if (!saved && ioctl(slave, TIOCSWINSZ, &size) < 0) saved = errno;
        for (int fd = 0; fd < 3 && !saved; ++fd) if (dup2(slave, fd) < 0) saved = errno;
        if (slave > 2) close(slave);
        close(p->master);
        if (!saved && !cwd.empty() && chdir(cwd.c_str())) saved = errno;
        if (!saved) {
            sigset_t empty; sigemptyset(&empty); sigprocmask(SIG_SETMASK, &empty, nullptr);
            for (int signal : {SIGINT, SIGQUIT, SIGTERM, SIGHUP, SIGPIPE, SIGTSTP, SIGTTIN, SIGTTOU}) ::signal(signal, SIG_DFL);
            execve(argv[0], argv.data(), envp.data()); saved = errno;
        }
        (void)write(handshake[1], &saved, sizeof saved); _exit(127);
    }
    close(handshake[1]);
    if (p->child < 0) { auto result = failure(e, "Cannot fork terminal"); close(handshake[0]); terminal_transport_destroy(t); return result; }
    int saved = 0; ssize_t count;
    do { count = read(handshake[0], &saved, sizeof saved); } while (count < 0 && errno == EINTR);
    close(handshake[0]);
    if (count != 0) {
        if (count < 0) saved = errno;
        kill(p->child, SIGKILL); while (waitpid(p->child, nullptr, 0) < 0 && errno == EINTR) {}
        p->child = -1; terminal_transport_destroy(t); errno = saved; return failure(e, "Cannot start terminal shell");
    }
    nonblocking(p->master); t->transport = p; t->running = true;
    try { t->writer = std::thread(writer, t, p); t->reader = std::thread(reader, t, p); }
    catch (...) { terminal_transport_destroy(t); return terminal_result(e, AXYNE_STATUS_IO_ERROR, "Cannot create terminal workers"); }
    return terminal_result(e, AXYNE_STATUS_OK);
}
AxyneStatus terminal_transport_resize(AxyneTerminal *t, int rows, int cols, AxyneError *e) {
    auto *p = static_cast<PosixTerminal *>(t->transport);
    winsize size{}; size.ws_row = static_cast<unsigned short>(rows); size.ws_col = static_cast<unsigned short>(cols);
    if (p && ioctl(p->master, TIOCSWINSZ, &size) < 0) return failure(e, "Cannot resize terminal");
    return terminal_result(e, AXYNE_STATUS_OK);
}
void terminal_transport_stop(AxyneTerminal *t) {
    auto *p = static_cast<PosixTerminal *>(t->transport);
    t->stopping = true; t->queue_ready.notify_all();
    if (!p || p->child <= 0) return;
    std::lock_guard<std::mutex> lock(p->child_mutex);
    siginfo_t info{};
    if (waitid(P_PID, p->child, &info, WEXITED | WNOHANG | WNOWAIT) < 0 && errno == ECHILD) {
        p->child = -1; return; // Never signal an externally reaped/reused ID.
    }
    // The shell's session/process-group leader remains owned and unreaped.
    // A terminal hangup reaches its foreground group via the tty driver;
    // do not signal a numeric foreground PGID which might have been reused.
    if (!p->child_exited) kill(p->child, SIGHUP);
    kill(-p->child, SIGKILL);
    kill(p->child, SIGKILL);
}
void terminal_transport_destroy(AxyneTerminal *t) {
    terminal_transport_stop(t);
    if (t->reader.joinable()) t->reader.join();
    if (t->writer.joinable()) t->writer.join();
    auto *p = static_cast<PosixTerminal *>(t->transport);
    if (p) {
        // Closing the controlling master hangs up any remaining foreground job.
        if (p->master >= 0) close(p->master);
        if (p->child > 0) while (waitpid(p->child, nullptr, 0) < 0 && errno == EINTR) {}
        delete p; t->transport = nullptr;
    }
    t->running = false;
}
