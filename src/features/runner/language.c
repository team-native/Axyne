#include "axyne/language.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define LANGUAGE_SEPARATOR '\\'
#define LANGUAGE_EXE_SUFFIX ".exe"
#else
#define LANGUAGE_SEPARATOR '/'
#define LANGUAGE_EXE_SUFFIX ""
#endif

typedef struct LanguageInfo {
    const char *identifier;
    const char *name;
    const char *extensions[6];
    AxyneRuntimeKind runtimes[AXYNE_LANGUAGE_MAX_RUNTIMES];
    size_t runtime_count;
    int configuration_applicable;
    int architecture_applicable;
} LanguageInfo;

/* Indexed by AxyneLanguageId. */
static const LanguageInfo languages[AXYNE_LANGUAGE_COUNT] = {
    { "python", "Python", { "py", NULL },
      { AXYNE_RUNTIME_PYTHON }, 1, 0, 0 },
    { "javascript", "JavaScript", { "js", "mjs", "cjs", "jsx", NULL },
      { AXYNE_RUNTIME_NODE }, 1, 0, 0 },
    { "typescript", "TypeScript", { "ts", "tsx", "mts", NULL },
      { AXYNE_RUNTIME_TYPESCRIPT, AXYNE_RUNTIME_NODE }, 2, 0, 0 },
    { "go", "Go", { "go", NULL },
      { AXYNE_RUNTIME_GO }, 1, 1, 1 },
    { "rust", "Rust", { "rs", NULL },
      { AXYNE_RUNTIME_RUST_CARGO, AXYNE_RUNTIME_RUSTC }, 2, 1, 1 },
    { "slint", "Slint", { "slint", NULL },
      { AXYNE_RUNTIME_SLINT }, 1, 0, 0 },
    { "kotlin", "Kotlin", { "kt", "kts", NULL },
      { AXYNE_RUNTIME_KOTLINC, AXYNE_RUNTIME_JAVA }, 2, 0, 0 },
    { "java", "Java", { "java", NULL },
      { AXYNE_RUNTIME_JAVAC, AXYNE_RUNTIME_JAVA }, 2, 1, 0 },
    { "swift", "Swift", { "swift", NULL },
      { AXYNE_RUNTIME_SWIFTC, AXYNE_RUNTIME_SWIFT }, 2, 1, 1 },
    { "c", "C", { "c", NULL },
      { AXYNE_RUNTIME_C }, 1, 1, 1 },
    { "cpp", "C++", { "cpp", "cc", "cxx", NULL },
      { AXYNE_RUNTIME_CPP }, 1, 1, 1 }
};

static const LanguageInfo *info_for(AxyneLanguageId language)
{
    if ((int)language < 0 || language >= AXYNE_LANGUAGE_COUNT) return NULL;
    return &languages[language];
}

const char *axyne_language_identifier(AxyneLanguageId language)
{
    const LanguageInfo *info = info_for(language);
    return info != NULL ? info->identifier : NULL;
}

const char *axyne_language_name(AxyneLanguageId language)
{
    const LanguageInfo *info = info_for(language);
    return info != NULL ? info->name : NULL;
}

const char *const *axyne_language_extensions(AxyneLanguageId language)
{
    const LanguageInfo *info = info_for(language);
    return info != NULL ? info->extensions : NULL;
}

AxyneLanguageId axyne_language_for_extension(const char *extension)
{
    int i;
    size_t j;
    if (extension == NULL) return AXYNE_LANGUAGE_NONE;
    if (extension[0] == '.') ++extension;
    if (extension[0] == '\0') return AXYNE_LANGUAGE_NONE;
    for (i = 0; i < AXYNE_LANGUAGE_COUNT; ++i) {
        for (j = 0; languages[i].extensions[j] != NULL; ++j) {
            const char *a = extension, *b = languages[i].extensions[j];
            while (*a != '\0' && *b != '\0' &&
                   tolower((unsigned char)*a) == (unsigned char)*b) { ++a; ++b; }
            if (*a == '\0' && *b == '\0') return (AxyneLanguageId)i;
        }
    }
    return AXYNE_LANGUAGE_NONE;
}

static const char *path_base(const char *path)
{
    const char *base = path, *cursor;
    for (cursor = path; *cursor != '\0'; ++cursor)
        if (*cursor == '/' || *cursor == '\\') base = cursor + 1;
    return base;
}

/* "/x", "\\x", "C:\\x", "C:/x" (a bare file name has no directory part and is
 * handled separately). */
static int path_is_absolute(const char *path)
{
    if (path[0] == '/' || path[0] == '\\') return 1;
    return isalpha((unsigned char)path[0]) && path[1] == ':' &&
           (path[2] == '/' || path[2] == '\\');
}

/* Last '.' of the base name, ignoring a leading dot ("." files have no ext). */
static const char *path_extension(const char *base)
{
    const char *dot = strrchr(base, '.');
    return (dot != NULL && dot != base) ? dot + 1 : NULL;
}

AxyneLanguageId axyne_language_for_path(const char *path)
{
    const char *extension;
    if (path == NULL) return AXYNE_LANGUAGE_NONE;
    extension = path_extension(path_base(path));
    return extension != NULL ? axyne_language_for_extension(extension)
                             : AXYNE_LANGUAGE_NONE;
}

size_t axyne_language_runtimes(AxyneLanguageId language,
                               AxyneRuntimeKind out[AXYNE_LANGUAGE_MAX_RUNTIMES])
{
    const LanguageInfo *info = info_for(language);
    size_t i;
    if (info == NULL || out == NULL) return 0;
    for (i = 0; i < info->runtime_count; ++i) out[i] = info->runtimes[i];
    return info->runtime_count;
}

void axyne_language_build_target_support(AxyneLanguageId language,
                                         int *configuration_applicable,
                                         int *architecture_applicable)
{
    const LanguageInfo *info = info_for(language);
    if (configuration_applicable != NULL)
        *configuration_applicable = info != NULL && info->configuration_applicable;
    if (architecture_applicable != NULL)
        *architecture_applicable = info != NULL && info->architecture_applicable;
}

/* ---- plan memory ---- */

static void step_free(AxyneLanguageStep *step)
{
    size_t i;
    free(step->executable);
    for (i = 0; i < step->argument_count; ++i) free(step->arguments[i]);
    free(step->arguments);
    for (i = 0; i < step->environment_count; ++i) free(step->environment[i]);
    free(step->environment);
    memset(step, 0, sizeof(*step));
}

void axyne_language_plan_free(AxyneLanguagePlan *plan)
{
    if (plan == NULL) return;
    step_free(&plan->build);
    step_free(&plan->run);
    free(plan->working_directory);
    free(plan->output_path);
    memset(plan, 0, sizeof(*plan));
    plan->language = AXYNE_LANGUAGE_NONE;
}

static char *copy_string(const char *text)
{
    size_t length = strlen(text);
    char *copy = (char *)malloc(length + 1);
    if (copy != NULL) memcpy(copy, text, length + 1);
    return copy;
}

static void set_message(char *message, size_t size, const char *format, const char *a,
                        const char *b)
{
    if (message == NULL || size == 0) return;
    if (format == NULL) { message[0] = '\0'; return; }
    (void)snprintf(message, size, format, a != NULL ? a : "", b != NULL ? b : "");
}

/* ---- template expansion ---- */

typedef struct Context {
    const char *file;
    const char *dir;
    const char *name;
    const char *out;
    const AxyneBuildFlags *flags;
} Context;

static char *expand_token(const char *token, const Context *context)
{
    static const char *const keys[] = { "{file}", "{dir}", "{out}", "{name}" };
    const char *values[4];
    size_t capacity = strlen(token) + 1, length = 0, i;
    char *result;
    values[0] = context->file; values[1] = context->dir;
    values[2] = context->out; values[3] = context->name;
    for (i = 0; i < 4; ++i) {
        const char *at = token;
        while ((at = strstr(at, keys[i])) != NULL) {
            capacity += values[i] != NULL ? strlen(values[i]) : 0;
            at += strlen(keys[i]);
        }
    }
    result = (char *)malloc(capacity);
    if (result == NULL) return NULL;
    while (*token != '\0') {
        int matched = 0;
        for (i = 0; i < 4; ++i) {
            size_t key_length = strlen(keys[i]);
            if (strncmp(token, keys[i], key_length) == 0) {
                size_t value_length = values[i] != NULL ? strlen(values[i]) : 0;
                if (value_length != 0) memcpy(result + length, values[i], value_length);
                length += value_length; token += key_length; matched = 1;
                break;
            }
        }
        if (!matched) result[length++] = *token++;
    }
    result[length] = '\0';
    return result;
}

/* Fills a step. `template_arguments` is NULL-terminated; the token "{flags}"
 * expands to the toolchain arguments, and the toolchain environment is
 * attached to the step. */
static int step_fill(AxyneLanguageStep *step, const char *executable,
                     const char *const *template_arguments, const Context *context,
                     AxyneRuntimeKind kind)
{
    size_t template_count = 0, i, flag_count = 0, index = 0;
    memset(step, 0, sizeof(*step));
    while (template_arguments != NULL && template_arguments[template_count] != NULL)
        ++template_count;
    if (context->flags != NULL) flag_count = context->flags->argument_count;
    step->executable = copy_string(executable);
    if (step->executable == NULL) return 0;
    step->has_runtime = 1;
    step->runtime_kind = kind;
    step->arguments = (char **)calloc(template_count + flag_count + 1, sizeof(char *));
    if (step->arguments == NULL) { step_free(step); return 0; }
    for (i = 0; i < template_count; ++i) {
        if (strcmp(template_arguments[i], "{flags}") == 0) {
            size_t f;
            for (f = 0; f < flag_count; ++f) {
                step->arguments[index] = copy_string(context->flags->arguments[f]);
                if (step->arguments[index] == NULL) { step->argument_count = index; step_free(step); return 0; }
                ++index;
            }
        } else {
            step->arguments[index] = expand_token(template_arguments[i], context);
            if (step->arguments[index] == NULL) { step->argument_count = index; step_free(step); return 0; }
            ++index;
        }
    }
    step->argument_count = index;
    if (context->flags != NULL && context->flags->environment_count != 0) {
        step->environment = (char **)calloc(context->flags->environment_count, sizeof(char *));
        if (step->environment == NULL) { step_free(step); return 0; }
        for (i = 0; i < context->flags->environment_count; ++i) {
            step->environment[i] = copy_string(context->flags->environment[i]);
            if (step->environment[i] == NULL) { step->environment_count = i; step_free(step); return 0; }
        }
        step->environment_count = context->flags->environment_count;
    }
    return 1;
}

/* ---- runtime lookup ---- */

static const char *find_runtime(const AxyneRuntimeList *runtimes, AxyneRuntimeKind kind)
{
    size_t i;
    if (runtimes == NULL) return NULL;
    for (i = 0; i < runtimes->count; ++i)
        if (runtimes->items[i].kind == kind && runtimes->items[i].executable != NULL &&
            runtimes->items[i].executable[0] != '\0')
            return runtimes->items[i].executable;
    return NULL;
}

static const char *runtime_hint(AxyneRuntimeKind kind)
{
    switch (kind) {
    case AXYNE_RUNTIME_PYTHON: return "python3, python";
    case AXYNE_RUNTIME_NODE: return "node";
    case AXYNE_RUNTIME_TYPESCRIPT: return "tsc";
    case AXYNE_RUNTIME_C: return "clang, gcc, cc";
    case AXYNE_RUNTIME_CPP: return "clang++, g++, c++";
    case AXYNE_RUNTIME_JAVA: return "java";
    case AXYNE_RUNTIME_JAVAC: return "javac";
    case AXYNE_RUNTIME_GO: return "go";
    case AXYNE_RUNTIME_RUST_CARGO: return "cargo";
    case AXYNE_RUNTIME_RUSTC: return "rustc";
    case AXYNE_RUNTIME_SLINT: return "slint-viewer";
    case AXYNE_RUNTIME_KOTLINC: return "kotlinc";
    case AXYNE_RUNTIME_SWIFTC: return "swiftc";
    case AXYNE_RUNTIME_SWIFT: return "swift";
    default: return "";
    }
}

/* Looks up every kind; on a miss writes the Korean message and returns 0. */
static int require_runtimes(const AxyneRuntimeList *runtimes, AxyneLanguageId language,
                            const AxyneRuntimeKind *kinds, size_t count,
                            const char **executables, char *message, size_t message_size)
{
    char names[160];
    size_t i, used = 0;
    int missing = 0;
    names[0] = '\0';
    for (i = 0; i < count; ++i) {
        executables[i] = find_runtime(runtimes, kinds[i]);
        if (executables[i] == NULL) {
            int written = snprintf(names + used, sizeof(names) - used, "%s%s",
                                   missing ? ", " : "", runtime_hint(kinds[i]));
            if (written > 0 && used + (size_t)written < sizeof(names)) used += (size_t)written;
            missing = 1;
        }
    }
    if (missing) {
        if (message != NULL && message_size != 0)
            (void)snprintf(message, message_size, "%s \xEB\x9F\xB0\xED\x83\x80\xEC\x9E\x84\xEC\x9D\x84 "
                           "\xEC\xB0\xBE\xEC\x9D\x84 \xEC\x88\x98 \xEC\x97\x86\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4 (%s)",
                           axyne_language_name(language), names);
    }
    return !missing;
}

/* ---- resolver ---- */

static const char *const t_file[] = { "{file}", NULL };
static const char *const t_none[] = { NULL };
static const char *const t_tsc[] = { "--target", "ES2020", "--module", "commonjs", "--skipLibCheck", "{file}", NULL };
static const char *const t_out[] = { "{out}", NULL };
static const char *const t_go_run[] = { "run", "{flags}", "{file}", NULL };
static const char *const t_cargo_build[] = { "build", "{flags}", "--manifest-path", "{dir}/Cargo.toml", NULL };
static const char *const t_cargo_run[] = { "run", "{flags}", "--manifest-path", "{dir}/Cargo.toml", NULL };
static const char *const t_rustc[] = { "{flags}", "{file}", "-o", "{out}", NULL };
static const char *const t_kotlin_build[] = { "{file}", "-include-runtime", "-d", "{out}", NULL };
static const char *const t_kotlin_script[] = { "-script", "{file}", NULL };
static const char *const t_java_run_jar[] = { "-jar", "{out}", NULL };
static const char *const t_javac[] = { "{flags}", "-d", "{dir}", "{file}", NULL };
static const char *const t_java_run[] = { "-cp", "{dir}", "{name}", NULL };
static const char *const t_cc[] = { "{flags}", "{file}", "-o", "{out}", NULL };
static const char *const t_cl[] = { "{flags}", "{file}", "/Fe:{out}", NULL };

/* Joins `dir` and `leaf`; no separator is added after a root such as "/",
 * "C:\\" or the drive-relative "C:". */
static void join_path(char *buffer, size_t size, const char *dir, char separator,
                      const char *leaf, const char *suffix)
{
    size_t length = strlen(dir);
    int bare = length != 0 && (dir[length - 1] == '/' || dir[length - 1] == '\\' ||
                               dir[length - 1] == ':');
    (void)snprintf(buffer, size, "%s%s%s%s", dir, bare ? "" : (separator == '\\' ? "\\" : "/"),
                   leaf, suffix);
}

static int has_extension(const char *base, const char *wanted)
{
    const char *extension = path_extension(base);
    size_t i;
    if (extension == NULL || strlen(extension) != strlen(wanted)) return 0;
    for (i = 0; wanted[i] != '\0'; ++i)
        if (tolower((unsigned char)extension[i]) != wanted[i]) return 0;
    return 1;
}

static int file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) return 0;
    fclose(file);
    return 1;
}

static AxyneStatus resolve_manual(const AxyneRunnerConfig *manual, const char *dir,
                                  AxyneLanguagePlan *plan, char *message, size_t message_size)
{
    size_t i;
    AxyneLanguageStep *run = &plan->run;
    plan->overridden = 1;
    run->executable = copy_string(manual->executable);
    if (run->executable == NULL) goto oom;
    if (manual->argument_count != 0) {
        run->arguments = (char **)calloc(manual->argument_count, sizeof(char *));
        if (run->arguments == NULL) goto oom;
        for (i = 0; i < manual->argument_count; ++i) {
            run->arguments[i] = copy_string(manual->arguments[i]);
            if (run->arguments[i] == NULL) goto oom;
            ++run->argument_count;
        }
    }
    if (manual->environment_count != 0) {
        run->environment = (char **)calloc(manual->environment_count, sizeof(char *));
        if (run->environment == NULL) goto oom;
        for (i = 0; i < manual->environment_count; ++i) {
            run->environment[i] = copy_string(manual->environment[i]);
            if (run->environment[i] == NULL) goto oom;
            ++run->environment_count;
        }
    }
    run->has_runtime = manual->has_runtime;
    run->runtime_kind = manual->runtime_kind;
    if (manual->working_directory != NULL && manual->working_directory[0] != '\0')
        plan->working_directory = copy_string(manual->working_directory);
    else if (dir != NULL)
        plan->working_directory = copy_string(dir);
    if (plan->working_directory == NULL &&
        ((manual->working_directory != NULL && manual->working_directory[0] != '\0') ||
         dir != NULL))
        goto oom;
    plan->toolchain = axyne_toolchain_from_executable(manual->executable);
    plan->toolchain_name = axyne_toolchain_name(plan->toolchain);
    set_message(message, message_size, NULL, NULL, NULL);
    return AXYNE_STATUS_OK;
oom:
    set_message(message, message_size, "out of memory%s%s", "", "");
    return AXYNE_STATUS_OUT_OF_MEMORY;
}

static int ends_with_ignore_case(const char *text, const char *suffix)
{
    size_t length = strlen(text), suffix_length = strlen(suffix), i;
    if (length < suffix_length) return 0;
    for (i = 0; i < suffix_length; ++i)
        if (tolower((unsigned char)text[length - suffix_length + i]) !=
            tolower((unsigned char)suffix[i])) return 0;
    return 1;
}

int axyne_language_is_command_script(const char *executable)
{
    return executable != NULL &&
           (ends_with_ignore_case(executable, ".cmd") ||
            ends_with_ignore_case(executable, ".bat"));
}

/* cmd.exe /s /c strips the first and last quote of the rest of the line, so
 * the script and its arguments are one argument: ""script" "a" "b"" (the same
 * convention as the runtime version probe). Quoted, so spaces and & | < > ^
 * are inert; '"' and '%' (expanded even inside quotes) and line breaks cannot
 * be neutralised and are refused. */
AxyneStatus axyne_language_step_wrap_command_script(AxyneLanguageStep *step,
                                                    const char *command_processor,
                                                    int windows)
{
    size_t length, i, used = 0;
    char *executable, *command, **arguments;
    if (step == NULL || command_processor == NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
    if (!windows || !axyne_language_is_command_script(step->executable)) return AXYNE_STATUS_OK;
    length = strlen(step->executable) + 5;
    for (i = 0; i < step->argument_count; ++i) length += strlen(step->arguments[i]) + 3;
    if (strpbrk(step->executable, "\"%\r\n") != NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
    for (i = 0; i < step->argument_count; ++i)
        if (strpbrk(step->arguments[i], "\"%\r\n") != NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
    command = (char *)malloc(length);
    executable = copy_string(command_processor);
    arguments = (char **)calloc(4, sizeof(char *));
    if (command == NULL || executable == NULL || arguments == NULL) goto oom;
    command[used++] = '"';
    command[used++] = '"';
    memcpy(command + used, step->executable, strlen(step->executable));
    used += strlen(step->executable);
    command[used++] = '"';
    for (i = 0; i < step->argument_count; ++i) {
        size_t n = strlen(step->arguments[i]);
        command[used++] = ' '; command[used++] = '"';
        memcpy(command + used, step->arguments[i], n); used += n;
        command[used++] = '"';
    }
    command[used++] = '"';
    command[used] = '\0';
    arguments[0] = copy_string("/d"); arguments[1] = copy_string("/s");
    arguments[2] = copy_string("/c"); arguments[3] = command;
    if (arguments[0] == NULL || arguments[1] == NULL || arguments[2] == NULL) goto oom;
    free(step->executable);
    for (i = 0; i < step->argument_count; ++i) free(step->arguments[i]);
    free(step->arguments);
    step->executable = executable;
    step->arguments = arguments;
    step->argument_count = 4;
    return AXYNE_STATUS_OK;
oom:
    free(command); free(executable);
    if (arguments != NULL) {
        free(arguments[0]); free(arguments[1]); free(arguments[2]);
        free(arguments);
    }
    return AXYNE_STATUS_OUT_OF_MEMORY;
}

#ifdef _WIN32
/* Absolute path of the system cmd.exe (UTF-8); CreateProcessW needs a full
 * application path. */
static char *system_command_processor(void)
{
    wchar_t directory[MAX_PATH], path[MAX_PATH];
    UINT length = GetSystemDirectoryW(directory, MAX_PATH);
    int bytes;
    char *utf8;
    if (length == 0 || length + 10 >= MAX_PATH) return NULL;
    memcpy(path, directory, ((size_t)length + 1) * sizeof(wchar_t));
    memcpy(path + length, L"\\cmd.exe", 9 * sizeof(wchar_t));
    bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, NULL, 0, NULL, NULL);
    if (bytes <= 0) return NULL;
    utf8 = (char *)malloc((size_t)bytes);
    if (utf8 != NULL && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1,
                                            utf8, bytes, NULL, NULL) <= 0) {
        free(utf8); return NULL;
    }
    return utf8;
}
#endif

AxyneStatus axyne_language_resolve_runner(AxyneLanguageId language,
                                          const AxyneRuntimeList *runtimes,
                                          const AxyneBuildTarget *target,
                                          const char *file_path,
                                          const AxyneRunnerConfig *manual_runner,
                                          AxyneLanguagePlan *plan,
                                          char *message, size_t message_size)
{
    AxyneBuildTarget local_target;
    AxyneBuildFlags flags;
    AxyneRuntimeKind kinds[AXYNE_LANGUAGE_MAX_RUNTIMES];
    const char *exe[AXYNE_LANGUAGE_MAX_RUNTIMES] = { NULL };
    const char *base = "", *separator_at, *cursor;
    char *dir = NULL, *name = NULL, *out = NULL;
    char separator = LANGUAGE_SEPARATOR;
    const char *out_suffix = "";
    AxyneToolchain toolchain = AXYNE_TOOLCHAIN_UNKNOWN;
    Context context;
    AxyneStatus status = AXYNE_STATUS_OK;
    size_t count, base_offset;
    int have_flags = 0, ok = 1;

    if (plan == NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
    memset(plan, 0, sizeof(*plan));
    plan->language = AXYNE_LANGUAGE_NONE;
    set_message(message, message_size, NULL, NULL, NULL);

    /* Directory of the file (when a path is given). */
    if (file_path != NULL && file_path[0] != '\0') {
        base = path_base(file_path);
        if (base != file_path && !path_is_absolute(file_path)) {
            set_message(message, message_size, "file path must be absolute%s%s", "", "");
            return AXYNE_STATUS_INVALID_ARGUMENT;
        }
        base_offset = (size_t)(base - file_path);
        separator_at = NULL;
        for (cursor = file_path; cursor < base; ++cursor)
            if (*cursor == '/' || *cursor == '\\') separator_at = cursor;
        if (separator_at != NULL) separator = *separator_at;
        if (base_offset == 0) dir = copy_string(".");
        else {
            size_t dir_length = base_offset - 1;
            if (dir_length == 0) dir_length = 1; /* "/file" -> "/" */
            else if (dir_length == 2 && file_path[1] == ':')
                dir_length = 3; /* "C:\\file" -> "C:\\", not the drive's cwd */
            else if (base_offset == 2 && file_path[1] == ':')
                dir_length = 2; /* "C:file" -> "C:" */
            dir = (char *)malloc(dir_length + 1);
            if (dir != NULL) { memcpy(dir, file_path, dir_length); dir[dir_length] = '\0'; }
        }
        if (dir == NULL) { set_message(message, message_size, "out of memory%s%s", "", ""); return AXYNE_STATUS_OUT_OF_MEMORY; }
    }

    if (manual_runner != NULL && manual_runner->executable != NULL &&
        manual_runner->executable[0] != '\0') {
        status = resolve_manual(manual_runner, dir, plan, message, message_size);
        plan->language = language;
        free(dir);
        if (status != AXYNE_STATUS_OK) axyne_language_plan_free(plan);
        return status;
    }

    if (file_path == NULL || file_path[0] == '\0' || dir == NULL) {
        set_message(message, message_size,
                    "\xED\x8C\x8C\xEC\x9D\xBC \xEA\xB2\xBD\xEB\xA1\x9C\xEA\xB0\x80 "
                    "\xED\x95\x84\xEC\x9A\x94\xED\x95\xA9\xEB\x8B\x88\xEB\x8B\xA4%s%s", "", "");
        free(dir);
        return AXYNE_STATUS_INVALID_ARGUMENT;
    }
    if (language == AXYNE_LANGUAGE_NONE) language = axyne_language_for_path(file_path);
    if (info_for(language) == NULL) {
        set_message(message, message_size,
                    "\xEC\xA7\x80\xEC\x9B\x90\xED\x95\x98\xEC\xA7\x80 \xEC\x95\x8A\xEB\x8A\x94 "
                    "\xEC\x96\xB8\xEC\x96\xB4\xEC\x9E\x85\xEB\x8B\x88\xEB\x8B\xA4: %s%s", base, "");
        free(dir);
        return AXYNE_STATUS_UNSUPPORTED;
    }
    plan->language = language;
    if (target == NULL) { local_target = axyne_build_target_default(); target = &local_target; }
    if (!axyne_build_target_valid(target)) {
        set_message(message, message_size, "invalid build target%s%s", "", "");
        free(dir);
        return AXYNE_STATUS_INVALID_ARGUMENT;
    }

    /* {name}: base name without its last extension. */
    {
        const char *extension = path_extension(base);
        size_t name_length = extension != NULL ? (size_t)(extension - base) - 1 : strlen(base);
        name = (char *)malloc(name_length + 1);
        if (name != NULL) { memcpy(name, base, name_length); name[name_length] = '\0'; }
    }
    if (name == NULL) { free(dir); set_message(message, message_size, "out of memory%s%s", "", ""); return AXYNE_STATUS_OUT_OF_MEMORY; }

    count = axyne_language_runtimes(language, kinds);
    if (language == AXYNE_LANGUAGE_RUST) {
        /* Either cargo or rustc is enough; decided below. */
        exe[0] = find_runtime(runtimes, kinds[0]);
        exe[1] = find_runtime(runtimes, kinds[1]);
    } else if (language == AXYNE_LANGUAGE_SWIFT) {
        exe[0] = find_runtime(runtimes, kinds[0]);
        exe[1] = find_runtime(runtimes, kinds[1]);
    } else if (language == AXYNE_LANGUAGE_KOTLIN && has_extension(base, "kts")) {
        if (!require_runtimes(runtimes, language, kinds, 1, exe, message, message_size)) {
            status = AXYNE_STATUS_NOT_FOUND; goto done;
        }
    } else if (!require_runtimes(runtimes, language, kinds, count, exe, message, message_size)) {
        status = AXYNE_STATUS_NOT_FOUND; goto done;
    }

    /* Toolchain and flags. */
    switch (language) {
    case AXYNE_LANGUAGE_C: case AXYNE_LANGUAGE_CPP:
        toolchain = axyne_toolchain_from_executable(exe[0]);
        if (toolchain != AXYNE_TOOLCHAIN_MSVC && toolchain != AXYNE_TOOLCHAIN_CLANG)
            toolchain = AXYNE_TOOLCHAIN_GCC;
        break;
    case AXYNE_LANGUAGE_GO: toolchain = AXYNE_TOOLCHAIN_GO; break;
    case AXYNE_LANGUAGE_JAVA: toolchain = AXYNE_TOOLCHAIN_JAVAC; break;
    case AXYNE_LANGUAGE_PYTHON: toolchain = AXYNE_TOOLCHAIN_PYTHON; break;
    case AXYNE_LANGUAGE_JAVASCRIPT: toolchain = AXYNE_TOOLCHAIN_NODE; break;
    case AXYNE_LANGUAGE_TYPESCRIPT: toolchain = AXYNE_TOOLCHAIN_TSC; break;
    case AXYNE_LANGUAGE_SLINT: toolchain = AXYNE_TOOLCHAIN_SLINT; break;
    case AXYNE_LANGUAGE_KOTLIN: toolchain = AXYNE_TOOLCHAIN_KOTLINC; break;
    case AXYNE_LANGUAGE_SWIFT: toolchain = AXYNE_TOOLCHAIN_SWIFT; break;
    default: break;
    }

    context.file = file_path; context.dir = dir; context.name = name; context.flags = NULL;

    /* Rust: cargo when Cargo.toml sits next to the file, otherwise rustc. */
    {
        int use_cargo = 0;
        if (language == AXYNE_LANGUAGE_RUST) {
            size_t manifest_size = strlen(dir) + 12;
            char *manifest = (char *)malloc(manifest_size);
            if (manifest == NULL) { status = AXYNE_STATUS_OUT_OF_MEMORY; goto done; }
            join_path(manifest, manifest_size, dir, separator, "Cargo.toml", "");
            use_cargo = exe[0] != NULL && file_exists(manifest);
            free(manifest);
            if (!use_cargo && exe[1] == NULL) {
                set_message(message, message_size,
                            "%s \xEB\x9F\xB0\xED\x83\x80\xEC\x9E\x84\xEC\x9D\x84 "
                            "\xEC\xB0\xBE\xEC\x9D\x84 \xEC\x88\x98 \xEC\x97\x86\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4 (%s)",
                            "Rust", exe[0] == NULL ? "cargo, rustc" : "rustc");
                status = AXYNE_STATUS_NOT_FOUND; goto done;
            }
            toolchain = use_cargo ? AXYNE_TOOLCHAIN_CARGO : AXYNE_TOOLCHAIN_RUSTC;
        }
        if (language == AXYNE_LANGUAGE_SWIFT) {
            if (exe[0] == NULL && exe[1] == NULL) {
                set_message(message, message_size,
                            "%s \xEB\x9F\xB0\xED\x83\x80\xEC\x9E\x84\xEC\x9D\x84 "
                            "\xEC\xB0\xBE\xEC\x9D\x84 \xEC\x88\x98 \xEC\x97\x86\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4 (%s)",
                            "Swift", "swiftc, swift");
                status = AXYNE_STATUS_NOT_FOUND; goto done;
            }
        }

        /* Output path. */
        switch (language) {
        case AXYNE_LANGUAGE_TYPESCRIPT: out_suffix = has_extension(base, "mts") ? ".mjs" : ".js"; break;
        case AXYNE_LANGUAGE_KOTLIN: out_suffix = ".jar"; break;
        case AXYNE_LANGUAGE_RUST: out_suffix = LANGUAGE_EXE_SUFFIX; break;
        case AXYNE_LANGUAGE_SWIFT: out_suffix = LANGUAGE_EXE_SUFFIX; break;
        case AXYNE_LANGUAGE_C: case AXYNE_LANGUAGE_CPP: out_suffix = LANGUAGE_EXE_SUFFIX; break;
        default: break;
        }
        {
            int needs_out = language == AXYNE_LANGUAGE_TYPESCRIPT || language == AXYNE_LANGUAGE_C ||
                            language == AXYNE_LANGUAGE_CPP || language == AXYNE_LANGUAGE_SWIFT ||
                            (language == AXYNE_LANGUAGE_KOTLIN && !has_extension(base, "kts")) ||
                            (language == AXYNE_LANGUAGE_RUST && !use_cargo);
            if (language == AXYNE_LANGUAGE_SWIFT && exe[0] == NULL) needs_out = 0;
            if (needs_out) {
                size_t length = strlen(dir) + 1 + strlen(name) + strlen(out_suffix) + 1;
                out = (char *)malloc(length);
                if (out == NULL) { status = AXYNE_STATUS_OUT_OF_MEMORY; goto done; }
                join_path(out, length, dir, separator, name, out_suffix);
            }
        }
        context.out = out;

        have_flags = axyne_build_target_flags(toolchain, target, &flags) == AXYNE_STATUS_OK;
        if (have_flags) context.flags = &flags;

        /* Steps. */
        switch (language) {
        case AXYNE_LANGUAGE_PYTHON:
            ok = step_fill(&plan->run, exe[0], t_file, &context, kinds[0]); break;
        case AXYNE_LANGUAGE_JAVASCRIPT:
            ok = step_fill(&plan->run, exe[0], t_file, &context, kinds[0]); break;
        case AXYNE_LANGUAGE_SLINT:
            ok = step_fill(&plan->run, exe[0], t_file, &context, kinds[0]); break;
        case AXYNE_LANGUAGE_TYPESCRIPT:
            context.flags = NULL;
            ok = step_fill(&plan->build, exe[0], t_tsc, &context, kinds[0]) &&
                 step_fill(&plan->run, exe[1], t_out, &context, kinds[1]);
            plan->has_build = 1; break;
        case AXYNE_LANGUAGE_GO:
            ok = step_fill(&plan->run, exe[0], t_go_run, &context, kinds[0]); break;
        case AXYNE_LANGUAGE_RUST:
            if (use_cargo) {
                ok = step_fill(&plan->build, exe[0], t_cargo_build, &context, kinds[0]) &&
                     step_fill(&plan->run, exe[0], t_cargo_run, &context, kinds[0]);
            } else {
                ok = step_fill(&plan->build, exe[1], t_rustc, &context, kinds[1]);
                if (ok) { context.flags = NULL; ok = step_fill(&plan->run, out, t_none, &context, kinds[1]); }
            }
            plan->has_build = 1; break;
        case AXYNE_LANGUAGE_KOTLIN:
            context.flags = NULL;
            if (has_extension(base, "kts")) {
                ok = step_fill(&plan->run, exe[0], t_kotlin_script, &context, kinds[0]);
            } else {
                ok = step_fill(&plan->build, exe[0], t_kotlin_build, &context, kinds[0]) &&
                     step_fill(&plan->run, exe[1], t_java_run_jar, &context, kinds[1]);
                plan->has_build = 1;
            }
            break;
        case AXYNE_LANGUAGE_JAVA:
            ok = step_fill(&plan->build, exe[0], t_javac, &context, kinds[0]);
            if (ok) { context.flags = NULL; ok = step_fill(&plan->run, exe[1], t_java_run, &context, kinds[1]); }
            plan->has_build = 1; break;
        case AXYNE_LANGUAGE_SWIFT:
            if (exe[0] != NULL) {
                ok = step_fill(&plan->build, exe[0], t_cc, &context, kinds[0]);
                if (ok) { context.flags = NULL; ok = step_fill(&plan->run, out, t_none, &context, kinds[0]); }
                plan->has_build = 1;
            } else {
                context.flags = NULL;
                ok = step_fill(&plan->run, exe[1], t_file, &context, kinds[1]);
                toolchain = AXYNE_TOOLCHAIN_SWIFT;
            }
            break;
        case AXYNE_LANGUAGE_C:
        case AXYNE_LANGUAGE_CPP:
            ok = step_fill(&plan->build, exe[0],
                           toolchain == AXYNE_TOOLCHAIN_MSVC ? t_cl : t_cc, &context, kinds[0]);
            if (ok) { context.flags = NULL; ok = step_fill(&plan->run, out, t_none, &context, kinds[0]); }
            plan->has_build = 1; break;
        default:
            ok = 0; break;
        }
    }
    if (!ok) { status = AXYNE_STATUS_OUT_OF_MEMORY; goto done; }
#ifdef _WIN32
    /* CreateProcessW cannot run .cmd/.bat (tsc.cmd, kotlinc.bat) directly. */
    {
        char *processor = NULL;
        if (axyne_language_is_command_script(plan->build.executable) ||
            axyne_language_is_command_script(plan->run.executable)) {
            processor = system_command_processor();
            if (processor == NULL) { status = AXYNE_STATUS_IO_ERROR; goto done; }
            status = axyne_language_step_wrap_command_script(&plan->build, processor, 1);
            if (status == AXYNE_STATUS_OK)
                status = axyne_language_step_wrap_command_script(&plan->run, processor, 1);
            free(processor);
            if (status != AXYNE_STATUS_OK) {
                set_message(message, message_size, "cannot run command script%s%s", "", "");
                goto done;
            }
        }
    }
#endif

    plan->working_directory = copy_string(dir);
    if (plan->working_directory == NULL) { status = AXYNE_STATUS_OUT_OF_MEMORY; goto done; }
    plan->output_path = out;
    out = NULL;
    plan->toolchain = toolchain;
    plan->toolchain_name = axyne_toolchain_name(toolchain);
done:
    if (status == AXYNE_STATUS_OUT_OF_MEMORY)
        set_message(message, message_size, "out of memory%s%s", "", "");
    free(dir); free(name); free(out);
    if (status != AXYNE_STATUS_OK) {
        AxyneLanguageId keep = plan->language;
        axyne_language_plan_free(plan);
        plan->language = keep;
    }
    return status;
}

void axyne_language_step_runner_spec(const AxyneLanguagePlan *plan,
                                     const AxyneLanguageStep *step,
                                     AxyneRunnerSpec *spec)
{
    if (spec == NULL) return;
    memset(spec, 0, sizeof(*spec));
    if (plan == NULL || step == NULL) return;
    spec->executable = step->executable;
    spec->arguments = (const char *const *)step->arguments;
    spec->argument_count = step->argument_count;
    spec->working_directory = plan->working_directory;
    spec->environment = (const char *const *)step->environment;
    spec->environment_count = step->environment_count;
    spec->has_runtime = step->has_runtime;
    spec->runtime_kind = step->runtime_kind;
}
