#include "test_support.h"

#include <string.h>

#include "axyne/build_target.h"

static AxyneBuildTarget target_of(AxynePlatform platform, AxyneConfiguration configuration,
                                  AxyneArchitecture architecture)
{
    AxyneBuildTarget target;
    target.platform = platform;
    target.configuration = configuration;
    target.architecture = architecture;
    return target;
}

static int flags_are(const AxyneBuildFlags *flags, const char *joined_arguments,
                     const char *joined_environment)
{
    char arguments[256] = "", environment[128] = "";
    size_t i;
    for (i = 0; i < flags->argument_count; ++i) {
        if (i != 0) strcat(arguments, "|");
        strcat(arguments, flags->arguments[i]);
    }
    for (i = 0; i < flags->environment_count; ++i) {
        if (i != 0) strcat(environment, "|");
        strcat(environment, flags->environment[i]);
    }
    if (strcmp(arguments, joined_arguments) != 0 ||
        strcmp(environment, joined_environment) != 0) {
        fprintf(stderr, "flags '%s' env '%s', expected '%s' env '%s'\n", arguments,
                environment, joined_arguments, joined_environment);
        return 0;
    }
    return 1;
}

static int check_flags(AxyneToolchain toolchain, AxyneBuildTarget target,
                       const char *arguments, const char *environment,
                       int configuration_applicable, int architecture_applicable)
{
    AxyneBuildFlags flags;
    AXYNE_TEST_STATUS(axyne_build_target_flags(toolchain, &target, &flags), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(flags_are(&flags, arguments, environment));
    AXYNE_TEST_EQ_INT(flags.configuration_applicable, configuration_applicable);
    AXYNE_TEST_EQ_INT(flags.architecture_applicable, architecture_applicable);
    return 1;
}

int axyne_test_build_target(const char *root)
{
    AxyneArchitecture options[AXYNE_ARCH_MAX_OPTIONS];
    AxyneBuildTarget target, other;
    AxyneBuildFlags flags;
    char label[64];
    char tiny[4];
    const AxynePlatform win = AXYNE_PLATFORM_WINDOWS, mac = AXYNE_PLATFORM_MACOS,
                        oth = AXYNE_PLATFORM_OTHER;
    const AxyneConfiguration dbg = AXYNE_CONFIGURATION_DEBUG, rel = AXYNE_CONFIGURATION_RELEASE;
    (void)root; (void)axyne_test_path; (void)axyne_test_make_directory;

    /* Platform option lists. */
    AXYNE_TEST_EQ_INT(axyne_build_target_architectures(win, options), 3);
    AXYNE_TEST_CHECK(options[0] == AXYNE_ARCH_X64 && options[1] == AXYNE_ARCH_X86 &&
                     options[2] == AXYNE_ARCH_ARM64);
    AXYNE_TEST_EQ_INT(axyne_build_target_architectures(mac, options), 2);
    AXYNE_TEST_CHECK(options[0] == AXYNE_ARCH_ARM64 && options[1] == AXYNE_ARCH_X64);
    AXYNE_TEST_EQ_INT(axyne_build_target_architectures(oth, options), 2);
    AXYNE_TEST_CHECK(!axyne_build_target_architecture_allowed(mac, AXYNE_ARCH_X86));
    AXYNE_TEST_CHECK(axyne_build_target_architecture_allowed(win, AXYNE_ARCH_X86));
    AXYNE_TEST_CHECK(strcmp(axyne_architecture_name(win, AXYNE_ARCH_X64), "x64") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_architecture_name(mac, AXYNE_ARCH_X64), "x86_64") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_architecture_name(mac, AXYNE_ARCH_ARM64), "arm64") == 0);

    /* Defaults: Debug plus the host architecture, falling back to the first
     * offered one when the platform does not offer the host architecture. */
    target = axyne_build_target_default_for(win, AXYNE_ARCH_X64);
    AXYNE_TEST_CHECK(target.configuration == dbg && target.architecture == AXYNE_ARCH_X64);
    target = axyne_build_target_default_for(mac, AXYNE_ARCH_ARM64);
    AXYNE_TEST_CHECK(target.architecture == AXYNE_ARCH_ARM64 && target.platform == mac);
    target = axyne_build_target_default_for(mac, AXYNE_ARCH_X86);
    AXYNE_TEST_CHECK(target.architecture == AXYNE_ARCH_ARM64);
    target = axyne_build_target_default();
    AXYNE_TEST_CHECK(axyne_build_target_valid(&target) && target.configuration == dbg);
    other = target; other.architecture = AXYNE_ARCH_X86; other.platform = mac;
    AXYNE_TEST_CHECK(!axyne_build_target_valid(&other));
    AXYNE_TEST_CHECK(!axyne_build_target_valid(NULL));

    /* Labels. */
    target = target_of(win, dbg, AXYNE_ARCH_X64);
    AXYNE_TEST_STATUS(axyne_build_target_label(&target, "MSVC", label, sizeof(label)), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(strcmp(label, "Debug \xC2\xB7 x64 (MSVC)") == 0);
    target = target_of(mac, dbg, AXYNE_ARCH_ARM64);
    AXYNE_TEST_STATUS(axyne_build_target_label(&target, "", label, sizeof(label)), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(strcmp(label, "Debug \xC2\xB7 arm64") == 0);
    AXYNE_TEST_STATUS(axyne_build_target_label(&target, NULL, label, sizeof(label)), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(strcmp(label, "Debug \xC2\xB7 arm64") == 0);
    target = target_of(mac, rel, AXYNE_ARCH_X64);
    AXYNE_TEST_STATUS(axyne_build_target_label(&target, "clang", label, sizeof(label)), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(strcmp(label, "Release \xC2\xB7 x86_64 (clang)") == 0);
    AXYNE_TEST_STATUS(axyne_build_target_label(&target, "clang", tiny, sizeof(tiny)),
                      AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_CHECK(tiny[0] == '\0');
    AXYNE_TEST_STATUS(axyne_build_target_label(NULL, "x", label, sizeof(label)),
                      AXYNE_STATUS_INVALID_ARGUMENT);
    other = target_of(mac, dbg, AXYNE_ARCH_X86);
    AXYNE_TEST_STATUS(axyne_build_target_label(&other, "x", label, sizeof(label)),
                      AXYNE_STATUS_INVALID_ARGUMENT);

    /* Toolchain names from executable paths. */
    AXYNE_TEST_CHECK(strcmp(axyne_toolchain_name_for_executable("C:\\VC\\bin\\CL.EXE"), "MSVC") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_toolchain_name_for_executable("/usr/bin/clang++"), "clang") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_toolchain_name_for_executable("/usr/bin/clang"), "clang") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_toolchain_name_for_executable("/usr/bin/gcc-13"), "gcc") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_toolchain_name_for_executable("/usr/bin/g++"), "gcc") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_toolchain_name_for_executable("/usr/bin/cc"), "gcc") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_toolchain_name_for_executable("/usr/bin/c++"), "gcc") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_toolchain_name_for_executable("/usr/local/go/bin/go"), "Go") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_toolchain_name_for_executable("cargo"), "Rust") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_toolchain_name_for_executable("rustc"), "Rust") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_toolchain_name_for_executable("kotlinc"), "Kotlin") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_toolchain_name_for_executable("kotlinc.bat"), "Kotlin") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_toolchain_name_for_executable("/jdk/bin/javac"), "javac") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_toolchain_name_for_executable("/jdk/bin/java"), "Java") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_toolchain_name_for_executable("swiftc"), "Swift") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_toolchain_name_for_executable("/usr/bin/python3.11"), "Python") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_toolchain_name_for_executable("python"), "Python") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_toolchain_name_for_executable("node"), "Node.js") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_toolchain_name_for_executable("tsc.cmd"), "TypeScript") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_toolchain_name_for_executable("slint-viewer"), "Slint") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_toolchain_name_for_executable("/bin/unknowntool"), "") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_toolchain_name_for_executable(""), "") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_toolchain_name_for_executable(NULL), "") == 0);
    AXYNE_TEST_CHECK(strcmp(axyne_toolchain_name_for_executable("123"), "") == 0);

    /* gcc / clang. */
    AXYNE_TEST_CHECK(check_flags(AXYNE_TOOLCHAIN_CLANG, target_of(mac, dbg, AXYNE_ARCH_ARM64),
                                 "-g|-O0|-arch|arm64", "", 1, 1));
    AXYNE_TEST_CHECK(check_flags(AXYNE_TOOLCHAIN_GCC, target_of(mac, rel, AXYNE_ARCH_X64),
                                 "-O2|-arch|x86_64", "", 1, 1));
    AXYNE_TEST_CHECK(check_flags(AXYNE_TOOLCHAIN_GCC, target_of(oth, dbg, AXYNE_ARCH_X64),
                                 "-g|-O0|-m64", "", 1, 1));
    AXYNE_TEST_CHECK(check_flags(AXYNE_TOOLCHAIN_GCC, target_of(win, rel, AXYNE_ARCH_X86),
                                 "-O2|-m32", "", 1, 1));
    AXYNE_TEST_CHECK(check_flags(AXYNE_TOOLCHAIN_GCC, target_of(win, dbg, AXYNE_ARCH_ARM64),
                                 "-g|-O0", "", 1, 1));
    /* MSVC: arch recorded only. */
    AXYNE_TEST_CHECK(check_flags(AXYNE_TOOLCHAIN_MSVC, target_of(win, dbg, AXYNE_ARCH_X86),
                                 "/Zi|/Od", "", 1, 1));
    AXYNE_TEST_CHECK(check_flags(AXYNE_TOOLCHAIN_MSVC, target_of(win, rel, AXYNE_ARCH_X64),
                                 "/O2", "", 1, 1));
    /* Go. */
    AXYNE_TEST_CHECK(check_flags(AXYNE_TOOLCHAIN_GO, target_of(win, dbg, AXYNE_ARCH_X86),
                                 "-gcflags=all=-N -l", "GOOS=windows|GOARCH=386", 1, 1));
    AXYNE_TEST_CHECK(check_flags(AXYNE_TOOLCHAIN_GO, target_of(mac, rel, AXYNE_ARCH_ARM64),
                                 "", "GOOS=darwin|GOARCH=arm64", 1, 1));
    AXYNE_TEST_CHECK(check_flags(AXYNE_TOOLCHAIN_GO, target_of(mac, rel, AXYNE_ARCH_X64),
                                 "", "GOOS=darwin|GOARCH=amd64", 1, 1));
    /* Rust. */
    AXYNE_TEST_CHECK(check_flags(AXYNE_TOOLCHAIN_CARGO, target_of(mac, rel, AXYNE_ARCH_ARM64),
                                 "--release|--target|aarch64-apple-darwin", "", 1, 1));
    AXYNE_TEST_CHECK(check_flags(AXYNE_TOOLCHAIN_CARGO, target_of(win, dbg, AXYNE_ARCH_X64),
                                 "--target|x86_64-pc-windows-msvc", "", 1, 1));
    AXYNE_TEST_CHECK(check_flags(AXYNE_TOOLCHAIN_RUSTC, target_of(win, rel, AXYNE_ARCH_ARM64),
                                 "-Copt-level=3|--target|aarch64-pc-windows-msvc", "", 1, 1));
    AXYNE_TEST_CHECK(check_flags(AXYNE_TOOLCHAIN_RUSTC, target_of(win, dbg, AXYNE_ARCH_X86),
                                 "-Copt-level=0|-Cdebuginfo=2|--target|i686-pc-windows-msvc", "", 1, 1));
    AXYNE_TEST_CHECK(check_flags(AXYNE_TOOLCHAIN_RUSTC, target_of(mac, dbg, AXYNE_ARCH_X64),
                                 "-Copt-level=0|-Cdebuginfo=2|--target|x86_64-apple-darwin", "", 1, 1));
    /* Swift. */
    AXYNE_TEST_CHECK(check_flags(AXYNE_TOOLCHAIN_SWIFT, target_of(mac, dbg, AXYNE_ARCH_ARM64),
                                 "-Onone|-g|-target|arm64-apple-macosx", "", 1, 1));
    AXYNE_TEST_CHECK(check_flags(AXYNE_TOOLCHAIN_SWIFT, target_of(mac, rel, AXYNE_ARCH_X64),
                                 "-O|-target|x86_64-apple-macosx", "", 1, 1));
    AXYNE_TEST_CHECK(check_flags(AXYNE_TOOLCHAIN_SWIFT, target_of(oth, rel, AXYNE_ARCH_X64),
                                 "-O", "", 1, 1));
    /* Java: configuration only; architecture not applicable. */
    AXYNE_TEST_CHECK(check_flags(AXYNE_TOOLCHAIN_JAVAC, target_of(mac, dbg, AXYNE_ARCH_ARM64),
                                 "-g", "", 1, 0));
    AXYNE_TEST_CHECK(check_flags(AXYNE_TOOLCHAIN_JAVAC, target_of(mac, rel, AXYNE_ARCH_ARM64),
                                 "-g:none", "", 1, 0));
    /* No effect. */
    AXYNE_TEST_CHECK(check_flags(AXYNE_TOOLCHAIN_KOTLINC, target_of(mac, dbg, AXYNE_ARCH_ARM64), "", "", 0, 0));
    AXYNE_TEST_CHECK(check_flags(AXYNE_TOOLCHAIN_PYTHON, target_of(mac, dbg, AXYNE_ARCH_ARM64), "", "", 0, 0));
    AXYNE_TEST_CHECK(check_flags(AXYNE_TOOLCHAIN_NODE, target_of(mac, dbg, AXYNE_ARCH_ARM64), "", "", 0, 0));
    AXYNE_TEST_CHECK(check_flags(AXYNE_TOOLCHAIN_TSC, target_of(mac, dbg, AXYNE_ARCH_ARM64), "", "", 0, 0));
    AXYNE_TEST_CHECK(check_flags(AXYNE_TOOLCHAIN_SLINT, target_of(mac, dbg, AXYNE_ARCH_ARM64), "", "", 0, 0));
    AXYNE_TEST_CHECK(check_flags(AXYNE_TOOLCHAIN_UNKNOWN, target_of(mac, dbg, AXYNE_ARCH_ARM64), "", "", 0, 0));

    /* Invalid input. */
    other = target_of(mac, dbg, AXYNE_ARCH_X86);
    AXYNE_TEST_STATUS(axyne_build_target_flags(AXYNE_TOOLCHAIN_GCC, &other, &flags),
                      AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_STATUS(axyne_build_target_flags(AXYNE_TOOLCHAIN_GCC, NULL, &flags),
                      AXYNE_STATUS_INVALID_ARGUMENT);
    AXYNE_TEST_STATUS(axyne_build_target_flags(AXYNE_TOOLCHAIN_GCC, &target, NULL),
                      AXYNE_STATUS_INVALID_ARGUMENT);
    return 1;
}
