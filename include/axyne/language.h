#ifndef AXYNE_LANGUAGE_H
#define AXYNE_LANGUAGE_H

#include <stddef.h>

#include "axyne/build_target.h"
#include "axyne/runner.h"
#include "axyne/runtime.h"
#include "axyne/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Language registry and per-language runner resolver. Runtimes are never
 * bundled: the resolver only consumes an AxyneRuntimeList that the caller
 * produced with axyne_runtime_discover (lazy, explicit). Nothing in this
 * module starts a process or touches a shell; commands are argv vectors. */

typedef enum AxyneLanguageId {
    AXYNE_LANGUAGE_NONE = -1,
    AXYNE_LANGUAGE_PYTHON = 0,
    AXYNE_LANGUAGE_JAVASCRIPT,
    AXYNE_LANGUAGE_TYPESCRIPT,
    AXYNE_LANGUAGE_GO,
    AXYNE_LANGUAGE_RUST,
    AXYNE_LANGUAGE_SLINT,
    AXYNE_LANGUAGE_KOTLIN,
    AXYNE_LANGUAGE_JAVA,
    AXYNE_LANGUAGE_SWIFT,
    AXYNE_LANGUAGE_C,
    AXYNE_LANGUAGE_CPP,
    AXYNE_LANGUAGE_COUNT
} AxyneLanguageId;

#define AXYNE_LANGUAGE_MAX_RUNTIMES 4

/* "python", "javascript", "typescript", "go", "rust", "slint", "kotlin",
 * "java", "swift", "c", "cpp"; NULL for NONE / out of range. */
const char *axyne_language_identifier(AxyneLanguageId language);
/* "Python", "JavaScript", "TypeScript", "Go", "Rust", "Slint", "Kotlin",
 * "Java", "Swift", "C", "C++". */
const char *axyne_language_name(AxyneLanguageId language);
/* NULL-terminated list of lowercase extensions without a dot. */
const char *const *axyne_language_extensions(AxyneLanguageId language);
/* Case-insensitive; accepts "py" or ".py". */
AxyneLanguageId axyne_language_for_extension(const char *extension);
/* Looks at the last path component only; NONE when unknown or no extension. */
AxyneLanguageId axyne_language_for_path(const char *path);

/* Runtime kinds the language may need (Kotlin: kotlinc and java; TypeScript:
 * tsc and node; Rust: cargo and rustc, either is enough). Returns the count. */
size_t axyne_language_runtimes(AxyneLanguageId language,
                               AxyneRuntimeKind out[AXYNE_LANGUAGE_MAX_RUNTIMES]);
/* Whether the Configuration and Architecture groups of the build-target
 * dropdown apply to the language, so the UI can hide the architecture group:
 *   configuration + architecture: c, cpp, go, rust, swift
 *   configuration only:           java (javac -g / -g:none)
 *   neither:                      python, javascript, typescript, slint, kotlin */
void axyne_language_build_target_support(AxyneLanguageId language,
                                         int *configuration_applicable,
                                         int *architecture_applicable);

/* One process invocation. Strings are owned by the plan. */
typedef struct AxyneLanguageStep {
    char *executable;
    char **arguments;
    size_t argument_count;
    char **environment; /* "KEY=VALUE" */
    size_t environment_count;
    int has_runtime;
    AxyneRuntimeKind runtime_kind;
} AxyneLanguageStep;

typedef struct AxyneLanguagePlan {
    AxyneLanguageId language;
    int overridden;  /* 1: the manual global runner was used, no build step */
    int has_build;   /* build is valid only when 1 */
    AxyneLanguageStep build;
    AxyneLanguageStep run;
    char *working_directory; /* directory of the file */
    char *output_path;       /* build output ({out}) or NULL */
    AxyneToolchain toolchain;
    const char *toolchain_name; /* static, "" when unknown */
} AxyneLanguagePlan;

/* Resolves the build and run steps for `file_path`.
 *   language       AXYNE_LANGUAGE_NONE derives it from the path.
 *   runtimes       result of axyne_runtime_discover (borrowed, may be NULL,
 *                  which counts as nothing installed).
 *   file_path      absolute path of the source file. A relative path with a
 *                  directory part ("src/a.c") is rejected with
 *                  INVALID_ARGUMENT; a bare file name resolves against ".".
 *   target         build target (may be NULL: default for the host).
 *   manual_runner  the user's configured global runner (may be NULL). When it
 *                  has an executable it overrides everything: no build step,
 *                  run step copied verbatim, language not consulted.
 *   plan           zero-initialised by the function; release with
 *                  axyne_language_plan_free (also after failure).
 *   message        optional UTF-8 text for the UI, always written when
 *                  message_size > 0 ("" on success).
 * Returns OK; NOT_FOUND with "<Language> 런타임을 찾을 수 없습니다 (<names>)"
 * when a needed runtime is missing; UNSUPPORTED for an unknown language;
 * INVALID_ARGUMENT for a missing path or invalid target.
 *
 * Placeholders in the command templates: {file} full path, {dir} its
 * directory, {name} file name without extension, {out} build output
 * (<dir>/<name>[.exe], .js for TypeScript, .jar for Kotlin). Templates:
 *   python  run: python3|python {file}
 *   js      run: node {file}
 *   ts      build: tsc --target ES2020 --module commonjs --skipLibCheck {file}
 *           run: node {out}
 *   go      run: go run [-gcflags=all=-N -l] {file}  (env GOOS, GOARCH)
 *   rust    Cargo.toml next to the file and cargo found:
 *             build: cargo build [--release] --target T --manifest-path {dir}/Cargo.toml
 *             run:   cargo run   (same flags)
 *           otherwise build: rustc <flags> {file} -o {out}; run: {out}
 *   slint   run: slint-viewer {file}
 *   kotlin  .kt  build: kotlinc {file} -include-runtime -d {out}; run: java -jar {out}
 *           .kts run: kotlinc -script {file}
 *   java    build: javac <flags> -d {dir} {file}; run: java -cp {dir} {name}
 *           (assumes no package declaration)
 *   swift   swiftc: build swiftc <flags> {file} -o {out}; run: {out}
 *           only swift found: run: swift {file}
 *   c, c++  build: <compiler> <flags> {file} -o {out} (cl: /Fe:{out}); run: {out}
 * The working directory is {dir}. */
AxyneStatus axyne_language_resolve_runner(AxyneLanguageId language,
                                          const AxyneRuntimeList *runtimes,
                                          const AxyneBuildTarget *target,
                                          const char *file_path,
                                          const AxyneRunnerConfig *manual_runner,
                                          AxyneLanguagePlan *plan,
                                          char *message, size_t message_size);
void axyne_language_plan_free(AxyneLanguagePlan *plan);

/* True for an executable path ending in .cmd or .bat (case-insensitive). */
int axyne_language_is_command_script(const char *executable);

/* When `windows` is non-zero and the step's executable is a .cmd/.bat script,
 * rewrites the step to run it through `command_processor` (the full path of
 * cmd.exe): arguments "/d" "/s" "/c" and one command string
 * ""script" "arg1" "arg2"" (the process layer passes that argument to
 * cmd.exe unescaped). Returns INVALID_ARGUMENT, leaving the step unchanged,
 * when the script or an argument contains '"', '%' or a line break. Other
 * steps and non-Windows platforms are left untouched. Exposed for tests. */
AxyneStatus axyne_language_step_wrap_command_script(AxyneLanguageStep *step,
                                                    const char *command_processor,
                                                    int windows);

/* Projects a step onto the existing runner contract. Strings are borrowed
 * from the plan. */
void axyne_language_step_runner_spec(const AxyneLanguagePlan *plan,
                                     const AxyneLanguageStep *step,
                                     AxyneRunnerSpec *spec);

#ifdef __cplusplus
}
#endif

#endif
