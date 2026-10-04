#include "test_support.h"

#include <string.h>

#include "axyne/language.h"

#ifdef _WIN32
#define EXE ".exe"
#else
#define EXE ""
#endif

/* Fake discovered runtimes; nothing here starts a process. */
typedef struct FakeRuntime { AxyneRuntimeKind kind; const char *path; } FakeRuntime;

static const FakeRuntime all_runtimes[] = {
    { AXYNE_RUNTIME_PYTHON, "/usr/bin/python3" },
    { AXYNE_RUNTIME_NODE, "/usr/bin/node" },
    { AXYNE_RUNTIME_TYPESCRIPT, "/usr/bin/tsc" },
    { AXYNE_RUNTIME_C, "/usr/bin/clang" },
    { AXYNE_RUNTIME_CPP, "/usr/bin/clang++" },
    { AXYNE_RUNTIME_JAVA, "/usr/bin/java" },
    { AXYNE_RUNTIME_JAVAC, "/usr/bin/javac" },
    { AXYNE_RUNTIME_GO, "/usr/local/go/bin/go" },
    { AXYNE_RUNTIME_RUST_CARGO, "/usr/bin/cargo" },
    { AXYNE_RUNTIME_RUSTC, "/usr/bin/rustc" },
    { AXYNE_RUNTIME_SLINT, "/usr/bin/slint-viewer" },
    { AXYNE_RUNTIME_KOTLINC, "/usr/bin/kotlinc" },
    { AXYNE_RUNTIME_SWIFTC, "/usr/bin/swiftc" },
    { AXYNE_RUNTIME_SWIFT, "/usr/bin/swift" }
};

typedef struct Fake {
    AxyneRuntime items[16];
    AxyneRuntimeList list;
} Fake;

/* Includes the runtimes of all_runtimes except the kinds in `skip` (list). */
static void fake_make(Fake *fake, const AxyneRuntimeKind *skip, size_t skip_count)
{
    size_t i, j;
    memset(fake, 0, sizeof(*fake));
    for (i = 0; i < sizeof(all_runtimes) / sizeof(all_runtimes[0]); ++i) {
        int skipped = 0;
        for (j = 0; j < skip_count; ++j)
            if (skip[j] == all_runtimes[i].kind) skipped = 1;
        if (skipped) continue;
        fake->items[fake->list.count].kind = all_runtimes[i].kind;
        fake->items[fake->list.count].executable = (char *)all_runtimes[i].path;
        fake->items[fake->list.count].version = (char *)"1";
        ++fake->list.count;
    }
    fake->list.items = fake->items;
}

static AxyneBuildTarget mac_arm_debug(void)
{
    AxyneBuildTarget target;
    target.platform = AXYNE_PLATFORM_MACOS;
    target.configuration = AXYNE_CONFIGURATION_DEBUG;
    target.architecture = AXYNE_ARCH_ARM64;
    return target;
}

static int args_are(char *const *arguments, size_t count, const char *joined)
{
    char text[512] = "";
    size_t i;
    for (i = 0; i < count; ++i) {
        if (i != 0) strcat(text, "|");
        strcat(text, arguments[i]);
    }
    if (strcmp(text, joined) != 0) {
        fprintf(stderr, "arguments '%s', expected '%s'\n", text, joined);
        return 0;
    }
    return 1;
}

#define STEP_IS(step, exe, joined) \
    (strcmp((step).executable, (exe)) == 0 && args_are((step).arguments, (step).argument_count, (joined)))

/* Resolves and returns the status; the plan is left for the caller. */
static AxyneStatus resolve(AxyneLanguageId language, const Fake *fake,
                           const AxyneBuildTarget *target, const char *path,
                           AxyneLanguagePlan *plan, char *message, size_t size)
{
    return axyne_language_resolve_runner(language, fake != NULL ? &fake->list : NULL,
                                         target, path, NULL, plan, message, size);
}

static int registry_checks(void)
{
    static const struct { const char *extension; AxyneLanguageId language; } map[] = {
        { "py", AXYNE_LANGUAGE_PYTHON }, { "PY", AXYNE_LANGUAGE_PYTHON },
        { ".py", AXYNE_LANGUAGE_PYTHON },
        { "js", AXYNE_LANGUAGE_JAVASCRIPT }, { "mjs", AXYNE_LANGUAGE_JAVASCRIPT },
        { "cjs", AXYNE_LANGUAGE_JAVASCRIPT }, { "jsx", AXYNE_LANGUAGE_JAVASCRIPT },
        { "ts", AXYNE_LANGUAGE_TYPESCRIPT }, { "tsx", AXYNE_LANGUAGE_TYPESCRIPT },
        { "mts", AXYNE_LANGUAGE_TYPESCRIPT },
        { "go", AXYNE_LANGUAGE_GO }, { "rs", AXYNE_LANGUAGE_RUST },
        { "slint", AXYNE_LANGUAGE_SLINT },
        { "kt", AXYNE_LANGUAGE_KOTLIN }, { "kts", AXYNE_LANGUAGE_KOTLIN },
        { "java", AXYNE_LANGUAGE_JAVA }, { "swift", AXYNE_LANGUAGE_SWIFT },
        { "c", AXYNE_LANGUAGE_C },
        { "cpp", AXYNE_LANGUAGE_CPP }, { "cc", AXYNE_LANGUAGE_CPP }, { "cxx", AXYNE_LANGUAGE_CPP },
        { "txt", AXYNE_LANGUAGE_NONE }, { "", AXYNE_LANGUAGE_NONE }, { ".", AXYNE_LANGUAGE_NONE }
    };
    AxyneRuntimeKind kinds[AXYNE_LANGUAGE_MAX_RUNTIMES];
    int cfg, arch, i;
    size_t k;
    for (k = 0; k < sizeof(map) / sizeof(map[0]); ++k)
        AXYNE_TEST_EQ_INT(axyne_language_for_extension(map[k].extension), map[k].language);
    AXYNE_TEST_EQ_INT(axyne_language_for_extension(NULL), AXYNE_LANGUAGE_NONE);

    AXYNE_TEST_EQ_INT(axyne_language_for_path("/a/b/main.go"), AXYNE_LANGUAGE_GO);
    AXYNE_TEST_EQ_INT(axyne_language_for_path("C:\\a\\Main.KT"), AXYNE_LANGUAGE_KOTLIN);
    AXYNE_TEST_EQ_INT(axyne_language_for_path("a.b.ts"), AXYNE_LANGUAGE_TYPESCRIPT);
    AXYNE_TEST_EQ_INT(axyne_language_for_path("/dir.go/file"), AXYNE_LANGUAGE_NONE);
    AXYNE_TEST_EQ_INT(axyne_language_for_path("/dir/.py"), AXYNE_LANGUAGE_NONE);
    AXYNE_TEST_EQ_INT(axyne_language_for_path("README"), AXYNE_LANGUAGE_NONE);
    AXYNE_TEST_EQ_INT(axyne_language_for_path(NULL), AXYNE_LANGUAGE_NONE);

    AXYNE_TEST_CHECK(axyne_language_identifier(AXYNE_LANGUAGE_NONE) == NULL);
    AXYNE_TEST_CHECK(axyne_language_name(AXYNE_LANGUAGE_COUNT) == NULL);
    AXYNE_TEST_CHECK(axyne_language_extensions(AXYNE_LANGUAGE_NONE) == NULL);
    AXYNE_TEST_CHECK(strcmp(axyne_language_name(AXYNE_LANGUAGE_CPP), "C++") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_language_name(AXYNE_LANGUAGE_GO), "Go") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_language_identifier(AXYNE_LANGUAGE_KOTLIN), "kotlin") == 0);

    /* Every registered extension maps back to its language. */
    for (i = 0; i < AXYNE_LANGUAGE_COUNT; ++i) {
        const char *const *extensions = axyne_language_extensions((AxyneLanguageId)i);
        AXYNE_TEST_CHECK(extensions != NULL && extensions[0] != NULL);
        AXYNE_TEST_CHECK(axyne_language_identifier((AxyneLanguageId)i) != NULL);
        for (; *extensions != NULL; ++extensions)
            AXYNE_TEST_EQ_INT(axyne_language_for_extension(*extensions), i);
    }

    AXYNE_TEST_EQ_INT(axyne_language_runtimes(AXYNE_LANGUAGE_TYPESCRIPT, kinds), 2);
    AXYNE_TEST_CHECK(kinds[0] == AXYNE_RUNTIME_TYPESCRIPT && kinds[1] == AXYNE_RUNTIME_NODE);
    AXYNE_TEST_EQ_INT(axyne_language_runtimes(AXYNE_LANGUAGE_GO, kinds), 1);
    AXYNE_TEST_CHECK(kinds[0] == AXYNE_RUNTIME_GO);
    AXYNE_TEST_EQ_INT(axyne_language_runtimes(AXYNE_LANGUAGE_NONE, kinds), 0);

    axyne_language_build_target_support(AXYNE_LANGUAGE_GO, &cfg, &arch);
    AXYNE_TEST_CHECK(cfg == 1 && arch == 1);
    axyne_language_build_target_support(AXYNE_LANGUAGE_JAVA, &cfg, &arch);
    AXYNE_TEST_CHECK(cfg == 1 && arch == 0);
    axyne_language_build_target_support(AXYNE_LANGUAGE_KOTLIN, &cfg, &arch);
    AXYNE_TEST_CHECK(cfg == 0 && arch == 0);
    axyne_language_build_target_support(AXYNE_LANGUAGE_PYTHON, &cfg, &arch);
    AXYNE_TEST_CHECK(cfg == 0 && arch == 0);
    axyne_language_build_target_support(AXYNE_LANGUAGE_SLINT, &cfg, &arch);
    AXYNE_TEST_CHECK(cfg == 0 && arch == 0);
    axyne_language_build_target_support(AXYNE_LANGUAGE_C, &cfg, &arch);
    AXYNE_TEST_CHECK(cfg == 1 && arch == 1);
    axyne_language_build_target_support(AXYNE_LANGUAGE_NONE, &cfg, &arch);
    AXYNE_TEST_CHECK(cfg == 0 && arch == 0);
    return 1;
}

static int scripting_checks(void)
{
    Fake fake;
    AxyneLanguagePlan plan;
    AxyneBuildTarget target = mac_arm_debug(), release = mac_arm_debug();
    char message[256];
    release.configuration = AXYNE_CONFIGURATION_RELEASE;
    fake_make(&fake, NULL, 0);

    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_PYTHON, &fake, &target, "/p/a.py", &plan, message, sizeof(message)), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(message[0] == '\0' && !plan.has_build && !plan.overridden);
    AXYNE_TEST_CHECK(STEP_IS(plan.run, "/usr/bin/python3", "/p/a.py"));
    AXYNE_TEST_CHECK(strcmp(plan.working_directory, "/p") == 0 && plan.output_path == NULL);
    AXYNE_TEST_CHECK(strcmp(plan.toolchain_name, "Python") == 0);
    axyne_language_plan_free(&plan);

    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_JAVASCRIPT, &fake, &target, "/p/a.mjs", &plan, message, sizeof(message)), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(STEP_IS(plan.run, "/usr/bin/node", "/p/a.mjs") && !plan.has_build);
    AXYNE_TEST_CHECK(strcmp(plan.toolchain_name, "Node.js") == 0);
    axyne_language_plan_free(&plan);

    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_NONE, &fake, &target, "/p/a.ts", &plan, message, sizeof(message)), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(plan.language == AXYNE_LANGUAGE_TYPESCRIPT && plan.has_build);
    AXYNE_TEST_CHECK(STEP_IS(plan.build, "/usr/bin/tsc",
                             "--target|ES2020|--module|commonjs|--skipLibCheck|/p/a.ts"));
    AXYNE_TEST_CHECK(STEP_IS(plan.run, "/usr/bin/node", "/p/a.js"));
    AXYNE_TEST_CHECK(strcmp(plan.output_path, "/p/a.js") == 0);
    axyne_language_plan_free(&plan);
    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_TYPESCRIPT, &fake, &target, "/p/a.mts", &plan, message, sizeof(message)), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(STEP_IS(plan.run, "/usr/bin/node", "/p/a.mjs"));
    axyne_language_plan_free(&plan);

    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_GO, &fake, &target, "/p/a.go", &plan, message, sizeof(message)), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(!plan.has_build);
    AXYNE_TEST_CHECK(STEP_IS(plan.run, "/usr/local/go/bin/go", "run|-gcflags=all=-N -l|/p/a.go"));
    AXYNE_TEST_EQ_INT(plan.run.environment_count, 2);
    AXYNE_TEST_CHECK(strcmp(plan.run.environment[0], "GOOS=darwin") == 0 &&
                     strcmp(plan.run.environment[1], "GOARCH=arm64") == 0);
    AXYNE_TEST_CHECK(strcmp(plan.toolchain_name, "Go") == 0);
    axyne_language_plan_free(&plan);
    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_GO, &fake, &release, "/p/a.go", &plan, message, sizeof(message)), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(STEP_IS(plan.run, "/usr/local/go/bin/go", "run|/p/a.go"));
    axyne_language_plan_free(&plan);

    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_SLINT, &fake, &target, "/p/ui.slint", &plan, message, sizeof(message)), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(STEP_IS(plan.run, "/usr/bin/slint-viewer", "/p/ui.slint") && !plan.has_build);
    axyne_language_plan_free(&plan);
    return 1;
}

static int compiled_checks(const char *root)
{
    Fake fake;
    AxyneLanguagePlan plan;
    AxyneBuildTarget target = mac_arm_debug(), release = mac_arm_debug();
    char message[256], file[1024], manifest[1100], expected[1200];
    static const AxyneRuntimeKind no_rustc[] = { AXYNE_RUNTIME_RUSTC };
    static const AxyneRuntimeKind no_cargo[] = { AXYNE_RUNTIME_RUST_CARGO };
    release.configuration = AXYNE_CONFIGURATION_RELEASE;
    fake_make(&fake, NULL, 0);

    /* C / C++ with the detected compiler. */
    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_C, &fake, &target, "/p/a.c", &plan, message, sizeof(message)), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(plan.has_build);
    AXYNE_TEST_CHECK(STEP_IS(plan.build, "/usr/bin/clang", "-g|-O0|-arch|arm64|/p/a.c|-o|/p/a" EXE));
    AXYNE_TEST_CHECK(STEP_IS(plan.run, "/p/a" EXE, ""));
    AXYNE_TEST_CHECK(strcmp(plan.toolchain_name, "clang") == 0);
    AXYNE_TEST_CHECK(strcmp(plan.output_path, "/p/a" EXE) == 0);
    axyne_language_plan_free(&plan);
    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_CPP, &fake, &release, "/p/a.cc", &plan, message, sizeof(message)), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(STEP_IS(plan.build, "/usr/bin/clang++", "-O2|-arch|arm64|/p/a.cc|-o|/p/a" EXE));
    axyne_language_plan_free(&plan);
    /* MSVC cl: argument form differs and the arch is recorded only. */
    fake.items[3].executable = (char *)"C:\\VC\\bin\\cl.exe";
    {
        AxyneBuildTarget win = target;
        win.platform = AXYNE_PLATFORM_WINDOWS; win.architecture = AXYNE_ARCH_X64;
        AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_C, &fake, &win, "C:/p/a.c", &plan, message, sizeof(message)), AXYNE_STATUS_OK);
        AXYNE_TEST_CHECK(STEP_IS(plan.build, "C:\\VC\\bin\\cl.exe", "/Zi|/Od|C:/p/a.c|/Fe:C:/p/a" EXE));
        AXYNE_TEST_CHECK(strcmp(plan.toolchain_name, "MSVC") == 0);
        AXYNE_TEST_CHECK(strcmp(plan.working_directory, "C:/p") == 0);
        axyne_language_plan_free(&plan);
    }
    fake.items[3].executable = (char *)"/usr/bin/cc";
    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_C, &fake, &target, "/p/a.c", &plan, message, sizeof(message)), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(strcmp(plan.toolchain_name, "gcc") == 0);
    axyne_language_plan_free(&plan);

    /* Rust: rustc for a lone file, cargo when Cargo.toml is next to it. */
    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_RUST, &fake, &target, "/nonexistent-axyne/x.rs", &plan, message, sizeof(message)), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(STEP_IS(plan.build, "/usr/bin/rustc",
                             "-Copt-level=0|-Cdebuginfo=2|--target|aarch64-apple-darwin|/nonexistent-axyne/x.rs|-o|/nonexistent-axyne/x" EXE));
    AXYNE_TEST_CHECK(STEP_IS(plan.run, "/nonexistent-axyne/x" EXE, ""));
    AXYNE_TEST_CHECK(strcmp(plan.toolchain_name, "Rust") == 0);
    axyne_language_plan_free(&plan);

    AXYNE_TEST_CHECK(axyne_test_register_cleanup(root));
    AXYNE_TEST_CHECK(axyne_test_make_directory(root));
    AXYNE_TEST_CHECK(axyne_test_path(file, sizeof(file), root, "main.rs"));
    AXYNE_TEST_CHECK(axyne_test_path(manifest, sizeof(manifest), root, "Cargo.toml"));
    AXYNE_TEST_CHECK(axyne_test_write(manifest, "[package]\nname = \"x\"\n"));
    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_RUST, &fake, &release, file, &plan, message, sizeof(message)), AXYNE_STATUS_OK);
    (void)snprintf(expected, sizeof(expected), "build|--release|--target|aarch64-apple-darwin|--manifest-path|%s/Cargo.toml", root);
    AXYNE_TEST_CHECK(STEP_IS(plan.build, "/usr/bin/cargo", expected));
    (void)snprintf(expected, sizeof(expected), "run|--release|--target|aarch64-apple-darwin|--manifest-path|%s/Cargo.toml", root);
    AXYNE_TEST_CHECK(STEP_IS(plan.run, "/usr/bin/cargo", expected));
    AXYNE_TEST_CHECK(plan.output_path == NULL && plan.has_build);
    axyne_language_plan_free(&plan);
    /* Manifest present but cargo missing: rustc is used. */
    fake_make(&fake, no_cargo, 1);
    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_RUST, &fake, &target, file, &plan, message, sizeof(message)), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(strcmp(plan.build.executable, "/usr/bin/rustc") == 0);
    axyne_language_plan_free(&plan);
    /* Neither tool, or only cargo without a manifest. */
    fake_make(&fake, no_rustc, 1);
    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_RUST, &fake, &target, "/nonexistent-axyne/x.rs", &plan, message, sizeof(message)), AXYNE_STATUS_NOT_FOUND);
    AXYNE_TEST_CHECK(strcmp(message, "Rust \xEB\x9F\xB0\xED\x83\x80\xEC\x9E\x84\xEC\x9D\x84 \xEC\xB0\xBE\xEC\x9D\x84 \xEC\x88\x98 \xEC\x97\x86\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4 (rustc)") == 0);
    axyne_language_plan_free(&plan);
    return 1;
}

static int jvm_swift_checks(void)
{
    Fake fake;
    AxyneLanguagePlan plan;
    AxyneBuildTarget target = mac_arm_debug();
    char message[256];
    static const AxyneRuntimeKind no_java[] = { AXYNE_RUNTIME_JAVA };
    static const AxyneRuntimeKind no_swiftc[] = { AXYNE_RUNTIME_SWIFTC };
    static const AxyneRuntimeKind no_swift_at_all[] = { AXYNE_RUNTIME_SWIFTC, AXYNE_RUNTIME_SWIFT };
    fake_make(&fake, NULL, 0);

    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_KOTLIN, &fake, &target, "/p/Main.kt", &plan, message, sizeof(message)), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(STEP_IS(plan.build, "/usr/bin/kotlinc", "/p/Main.kt|-include-runtime|-d|/p/Main.jar"));
    AXYNE_TEST_CHECK(STEP_IS(plan.run, "/usr/bin/java", "-jar|/p/Main.jar"));
    AXYNE_TEST_CHECK(strcmp(plan.toolchain_name, "Kotlin") == 0);
    axyne_language_plan_free(&plan);
    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_KOTLIN, &fake, &target, "/p/s.kts", &plan, message, sizeof(message)), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(!plan.has_build && STEP_IS(plan.run, "/usr/bin/kotlinc", "-script|/p/s.kts"));
    axyne_language_plan_free(&plan);
    fake_make(&fake, no_java, 1);
    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_KOTLIN, &fake, &target, "/p/Main.kt", &plan, message, sizeof(message)), AXYNE_STATUS_NOT_FOUND);
    AXYNE_TEST_CHECK(strcmp(message, "Kotlin \xEB\x9F\xB0\xED\x83\x80\xEC\x9E\x84\xEC\x9D\x84 \xEC\xB0\xBE\xEC\x9D\x84 \xEC\x88\x98 \xEC\x97\x86\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4 (java)") == 0);
    AXYNE_TEST_CHECK(plan.run.executable == NULL && plan.build.executable == NULL);
    axyne_language_plan_free(&plan);
    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_KOTLIN, &fake, &target, "/p/s.kts", &plan, message, sizeof(message)), AXYNE_STATUS_OK);
    axyne_language_plan_free(&plan);

    fake_make(&fake, NULL, 0);
    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_JAVA, &fake, &target, "/p/Main.java", &plan, message, sizeof(message)), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(STEP_IS(plan.build, "/usr/bin/javac", "-g|-d|/p|/p/Main.java"));
    AXYNE_TEST_CHECK(STEP_IS(plan.run, "/usr/bin/java", "-cp|/p|Main"));
    AXYNE_TEST_CHECK(strcmp(plan.toolchain_name, "javac") == 0);
    axyne_language_plan_free(&plan);

    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_SWIFT, &fake, &target, "/p/a.swift", &plan, message, sizeof(message)), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(STEP_IS(plan.build, "/usr/bin/swiftc", "-Onone|-g|-target|arm64-apple-macosx|/p/a.swift|-o|/p/a" EXE));
    AXYNE_TEST_CHECK(STEP_IS(plan.run, "/p/a" EXE, ""));
    AXYNE_TEST_CHECK(strcmp(plan.toolchain_name, "Swift") == 0);
    axyne_language_plan_free(&plan);
    fake_make(&fake, no_swiftc, 1);
    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_SWIFT, &fake, &target, "/p/a.swift", &plan, message, sizeof(message)), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(!plan.has_build && STEP_IS(plan.run, "/usr/bin/swift", "/p/a.swift"));
    AXYNE_TEST_CHECK(plan.output_path == NULL);
    axyne_language_plan_free(&plan);
    fake_make(&fake, no_swift_at_all, 2);
    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_SWIFT, &fake, &target, "/p/a.swift", &plan, message, sizeof(message)), AXYNE_STATUS_NOT_FOUND);
    AXYNE_TEST_CHECK(strcmp(message, "Swift \xEB\x9F\xB0\xED\x83\x80\xEC\x9E\x84\xEC\x9D\x84 \xEC\xB0\xBE\xEC\x9D\x84 \xEC\x88\x98 \xEC\x97\x86\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4 (swiftc, swift)") == 0);
    axyne_language_plan_free(&plan);
    return 1;
}

static int missing_and_override_checks(void)
{
    Fake empty, fake;
    AxyneLanguagePlan plan;
    AxyneBuildTarget target = mac_arm_debug();
    AxyneRunnerConfig manual;
    AxyneRunnerSpec spec;
    AxyneError error;
    char message[256], tiny[8];
    const char *manual_arguments[] = { "--x", "{file}" };
    const char *manual_environment[] = { "A=1" };
    static const AxyneRuntimeKind no_node[] = { AXYNE_RUNTIME_NODE };
    static const AxyneRuntimeKind no_tsc[] = { AXYNE_RUNTIME_TYPESCRIPT };
    static const AxyneRuntimeKind no_ts_pair[] = { AXYNE_RUNTIME_TYPESCRIPT, AXYNE_RUNTIME_NODE };
    static const struct { AxyneLanguageId language; const char *path; const char *message; } cases[] = {
        { AXYNE_LANGUAGE_PYTHON, "/p/a.py", "Python \xEB\x9F\xB0\xED\x83\x80\xEC\x9E\x84\xEC\x9D\x84 \xEC\xB0\xBE\xEC\x9D\x84 \xEC\x88\x98 \xEC\x97\x86\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4 (python3, python)" },
        { AXYNE_LANGUAGE_GO, "/p/a.go", "Go \xEB\x9F\xB0\xED\x83\x80\xEC\x9E\x84\xEC\x9D\x84 \xEC\xB0\xBE\xEC\x9D\x84 \xEC\x88\x98 \xEC\x97\x86\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4 (go)" },
        { AXYNE_LANGUAGE_C, "/p/a.c", "C \xEB\x9F\xB0\xED\x83\x80\xEC\x9E\x84\xEC\x9D\x84 \xEC\xB0\xBE\xEC\x9D\x84 \xEC\x88\x98 \xEC\x97\x86\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4 (clang, gcc, cc)" },
        { AXYNE_LANGUAGE_SLINT, "/p/a.slint", "Slint \xEB\x9F\xB0\xED\x83\x80\xEC\x9E\x84\xEC\x9D\x84 \xEC\xB0\xBE\xEC\x9D\x84 \xEC\x88\x98 \xEC\x97\x86\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4 (slint-viewer)" },
        { AXYNE_LANGUAGE_JAVA, "/p/A.java", "Java \xEB\x9F\xB0\xED\x83\x80\xEC\x9E\x84\xEC\x9D\x84 \xEC\xB0\xBE\xEC\x9D\x84 \xEC\x88\x98 \xEC\x97\x86\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4 (javac, java)" },
        { AXYNE_LANGUAGE_RUST, "/nonexistent-axyne/a.rs", "Rust \xEB\x9F\xB0\xED\x83\x80\xEC\x9E\x84\xEC\x9D\x84 \xEC\xB0\xBE\xEC\x9D\x84 \xEC\x88\x98 \xEC\x97\x86\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4 (cargo, rustc)" },
        { AXYNE_LANGUAGE_TYPESCRIPT, "/p/a.ts", "TypeScript \xEB\x9F\xB0\xED\x83\x80\xEC\x9E\x84\xEC\x9D\x84 \xEC\xB0\xBE\xEC\x9D\x84 \xEC\x88\x98 \xEC\x97\x86\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4 (tsc, node)" }
    };
    size_t i;
    memset(&empty, 0, sizeof(empty));

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        AXYNE_TEST_STATUS(resolve(cases[i].language, &empty, &target, cases[i].path, &plan, message, sizeof(message)), AXYNE_STATUS_NOT_FOUND);
        AXYNE_TEST_CHECK(strcmp(message, cases[i].message) == 0);
        AXYNE_TEST_CHECK(plan.run.executable == NULL && plan.build.executable == NULL);
        axyne_language_plan_free(&plan);
    }
    /* NULL runtime list counts as nothing installed; NULL message is allowed. */
    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_GO, NULL, &target, "/p/a.go", &plan, NULL, 0), AXYNE_STATUS_NOT_FOUND);
    axyne_language_plan_free(&plan);
    /* Truncated message buffer stays terminated. */
    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_GO, &empty, &target, "/p/a.go", &plan, tiny, sizeof(tiny)), AXYNE_STATUS_NOT_FOUND);
    AXYNE_TEST_CHECK(strlen(tiny) < sizeof(tiny));
    axyne_language_plan_free(&plan);

    fake_make(&fake, no_node, 1);
    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_TYPESCRIPT, &fake, &target, "/p/a.ts", &plan, message, sizeof(message)), AXYNE_STATUS_NOT_FOUND);
    AXYNE_TEST_CHECK(strstr(message, "(node)") != NULL);
    axyne_language_plan_free(&plan);
    fake_make(&fake, no_tsc, 1);
    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_TYPESCRIPT, &fake, &target, "/p/a.ts", &plan, message, sizeof(message)), AXYNE_STATUS_NOT_FOUND);
    AXYNE_TEST_CHECK(strstr(message, "(tsc)") != NULL);
    axyne_language_plan_free(&plan);
    fake_make(&fake, no_ts_pair, 2);
    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_JAVASCRIPT, &fake, &target, "/p/a.js", &plan, message, sizeof(message)), AXYNE_STATUS_NOT_FOUND);
    axyne_language_plan_free(&plan);

    /* Unsupported and invalid input. */
    fake_make(&fake, NULL, 0);
    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_NONE, &fake, &target, "/p/a.txt", &plan, message, sizeof(message)), AXYNE_STATUS_UNSUPPORTED);
    AXYNE_TEST_CHECK(message[0] != '\0');
    axyne_language_plan_free(&plan);
    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_NONE, &fake, &target, "README", &plan, message, sizeof(message)), AXYNE_STATUS_UNSUPPORTED);
    axyne_language_plan_free(&plan);
    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_GO, &fake, &target, NULL, &plan, message, sizeof(message)), AXYNE_STATUS_INVALID_ARGUMENT);
    axyne_language_plan_free(&plan);
    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_GO, &fake, &target, "", &plan, message, sizeof(message)), AXYNE_STATUS_INVALID_ARGUMENT);
    axyne_language_plan_free(&plan);
    {
        AxyneBuildTarget bad = target;
        bad.architecture = AXYNE_ARCH_X86;
        AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_GO, &fake, &bad, "/p/a.go", &plan, message, sizeof(message)), AXYNE_STATUS_INVALID_ARGUMENT);
        axyne_language_plan_free(&plan);
    }
    AXYNE_TEST_STATUS(axyne_language_resolve_runner(AXYNE_LANGUAGE_GO, &fake.list, &target, "/p/a.go", NULL, NULL, message, sizeof(message)), AXYNE_STATUS_INVALID_ARGUMENT);
    /* No target: the host default is used. */
    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_GO, &fake, NULL, "/p/a.go", &plan, message, sizeof(message)), AXYNE_STATUS_OK);
    axyne_language_plan_free(&plan);
    /* A file without directory resolves relative to ".". */
    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_PYTHON, &fake, &target, "a.py", &plan, message, sizeof(message)), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(strcmp(plan.working_directory, ".") == 0);
    axyne_language_plan_free(&plan);

    /* The manual global runner overrides everything, even a missing runtime. */
    AXYNE_TEST_STATUS(axyne_runner_initialize(&manual, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_language_resolve_runner(AXYNE_LANGUAGE_GO, &empty.list, &target, "/p/a.go", &manual, &plan, message, sizeof(message)), AXYNE_STATUS_NOT_FOUND);
    axyne_language_plan_free(&plan); /* unconfigured manual runner is ignored */
    memset(&spec, 0, sizeof(spec));
    spec.executable = "/bin/echo";
    spec.arguments = manual_arguments; spec.argument_count = 2;
    spec.environment = manual_environment; spec.environment_count = 1;
    spec.working_directory = "/w";
    spec.has_runtime = 1; spec.runtime_kind = AXYNE_RUNTIME_C;
    AXYNE_TEST_STATUS(axyne_runner_configure(&manual, &spec, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_STATUS(axyne_language_resolve_runner(AXYNE_LANGUAGE_GO, &empty.list, &target, "/p/a.go", &manual, &plan, message, sizeof(message)), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(plan.overridden && !plan.has_build && message[0] == '\0');
    AXYNE_TEST_CHECK(STEP_IS(plan.run, "/bin/echo", "--x|{file}")); /* no substitution */
    AXYNE_TEST_CHECK(plan.run.environment_count == 1 && strcmp(plan.run.environment[0], "A=1") == 0);
    AXYNE_TEST_CHECK(strcmp(plan.working_directory, "/w") == 0);
    AXYNE_TEST_CHECK(plan.run.has_runtime && plan.run.runtime_kind == AXYNE_RUNTIME_C);
    axyne_language_plan_free(&plan);
    /* Override without a file or language. */
    AXYNE_TEST_STATUS(axyne_language_resolve_runner(AXYNE_LANGUAGE_NONE, NULL, NULL, NULL, &manual, &plan, message, sizeof(message)), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(plan.overridden && plan.working_directory != NULL);
    axyne_language_plan_free(&plan);
    axyne_runner_destroy(&manual);
    return 1;
}

static int projection_checks(void)
{
    Fake fake;
    AxyneLanguagePlan plan;
    AxyneBuildTarget target = mac_arm_debug();
    AxyneRunnerSpec spec;
    AxyneRunnerConfig config;
    AxyneError error;
    char message[256];
    fake_make(&fake, NULL, 0);

    /* Every language produces steps accepted by the existing runner contract. */
    {
        static const char *const files[] = { "/p/a.py", "/p/a.js", "/p/a.ts", "/p/a.go",
            "/nonexistent-axyne/a.rs", "/p/a.slint", "/p/a.kt", "/p/A.java",
            "/p/a.swift", "/p/a.c", "/p/a.cpp" };
        size_t i;
        for (i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
            AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_NONE, &fake, &target, files[i], &plan, message, sizeof(message)), AXYNE_STATUS_OK);
            AXYNE_TEST_CHECK(plan.run.executable != NULL && plan.working_directory != NULL);
            if (plan.has_build) {
                axyne_language_step_runner_spec(&plan, &plan.build, &spec);
                AXYNE_TEST_STATUS(axyne_runner_initialize(&config, &error), AXYNE_STATUS_OK);
                AXYNE_TEST_STATUS(axyne_runner_configure(&config, &spec, &error), AXYNE_STATUS_OK);
                axyne_runner_destroy(&config);
            }
            axyne_language_step_runner_spec(&plan, &plan.run, &spec);
            AXYNE_TEST_STATUS(axyne_runner_initialize(&config, &error), AXYNE_STATUS_OK);
            AXYNE_TEST_STATUS(axyne_runner_configure(&config, &spec, &error), AXYNE_STATUS_OK);
            axyne_runner_destroy(&config);
            axyne_language_plan_free(&plan);
        }
    }
    AXYNE_TEST_STATUS(resolve(AXYNE_LANGUAGE_GO, &fake, &target, "/p/a.go", &plan, message, sizeof(message)), AXYNE_STATUS_OK);
    axyne_language_step_runner_spec(&plan, &plan.run, &spec);
    AXYNE_TEST_CHECK(strcmp(spec.executable, "/usr/local/go/bin/go") == 0 && spec.argument_count == 3 &&
                     strcmp(spec.working_directory, "/p") == 0 && spec.environment_count == 2 &&
                     spec.has_runtime && spec.runtime_kind == AXYNE_RUNTIME_GO);
    axyne_language_plan_free(&plan);
    axyne_language_plan_free(NULL);
    return 1;
}

int axyne_test_languages(const char *root)
{
    (void)axyne_test_make_directory;
    return registry_checks() && scripting_checks() && compiled_checks(root) &&
           jvm_swift_checks() && missing_and_override_checks() && projection_checks();
}
