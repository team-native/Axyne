#include "axyne/build_target.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

AxynePlatform axyne_build_target_host_platform(void)
{
#if defined(_WIN32)
    return AXYNE_PLATFORM_WINDOWS;
#elif defined(__APPLE__)
    return AXYNE_PLATFORM_MACOS;
#else
    return AXYNE_PLATFORM_OTHER;
#endif
}

AxyneArchitecture axyne_build_target_host_architecture(void)
{
#if defined(__aarch64__) || defined(_M_ARM64) || defined(__arm64__)
    return AXYNE_ARCH_ARM64;
#elif defined(__i386__) || defined(_M_IX86)
    return AXYNE_ARCH_X86;
#else
    return AXYNE_ARCH_X64;
#endif
}

size_t axyne_build_target_architectures(AxynePlatform platform,
                                        AxyneArchitecture out[AXYNE_ARCH_MAX_OPTIONS])
{
    if (out == NULL) return 0;
    switch (platform) {
    case AXYNE_PLATFORM_WINDOWS:
        out[0] = AXYNE_ARCH_X64; out[1] = AXYNE_ARCH_X86; out[2] = AXYNE_ARCH_ARM64;
        return 3;
    case AXYNE_PLATFORM_MACOS:
        out[0] = AXYNE_ARCH_ARM64; out[1] = AXYNE_ARCH_X64;
        return 2;
    default:
        out[0] = AXYNE_ARCH_X64; out[1] = AXYNE_ARCH_ARM64;
        return 2;
    }
}

int axyne_build_target_architecture_allowed(AxynePlatform platform,
                                            AxyneArchitecture architecture)
{
    AxyneArchitecture options[AXYNE_ARCH_MAX_OPTIONS];
    size_t count = axyne_build_target_architectures(platform, options), i;
    for (i = 0; i < count; ++i)
        if (options[i] == architecture) return 1;
    return 0;
}

const char *axyne_architecture_name(AxynePlatform platform,
                                    AxyneArchitecture architecture)
{
    switch (architecture) {
    case AXYNE_ARCH_X64:
        return platform == AXYNE_PLATFORM_MACOS ? "x86_64" : "x64";
    case AXYNE_ARCH_X86: return "x86";
    case AXYNE_ARCH_ARM64: return "arm64";
    }
    return "";
}

const char *axyne_configuration_name(AxyneConfiguration configuration)
{
    return configuration == AXYNE_CONFIGURATION_RELEASE ? "Release" : "Debug";
}

AxyneBuildTarget axyne_build_target_default_for(AxynePlatform platform,
                                                AxyneArchitecture host)
{
    AxyneBuildTarget target;
    AxyneArchitecture options[AXYNE_ARCH_MAX_OPTIONS];
    (void)axyne_build_target_architectures(platform, options);
    target.platform = platform;
    target.configuration = AXYNE_CONFIGURATION_DEBUG;
    target.architecture = axyne_build_target_architecture_allowed(platform, host)
                              ? host : options[0];
    return target;
}

AxyneBuildTarget axyne_build_target_default(void)
{
    return axyne_build_target_default_for(axyne_build_target_host_platform(),
                                          axyne_build_target_host_architecture());
}

int axyne_build_target_valid(const AxyneBuildTarget *target)
{
    return target != NULL &&
           (target->configuration == AXYNE_CONFIGURATION_DEBUG ||
            target->configuration == AXYNE_CONFIGURATION_RELEASE) &&
           axyne_build_target_architecture_allowed(target->platform,
                                                   target->architecture);
}

AxyneStatus axyne_build_target_label(const AxyneBuildTarget *target,
                                     const char *toolchain_name,
                                     char *buffer, size_t capacity)
{
    int written;
    if (buffer == NULL || capacity == 0) return AXYNE_STATUS_INVALID_ARGUMENT;
    buffer[0] = '\0';
    if (!axyne_build_target_valid(target)) return AXYNE_STATUS_INVALID_ARGUMENT;
    if (toolchain_name != NULL && toolchain_name[0] != '\0')
        written = snprintf(buffer, capacity, "%s \xC2\xB7 %s (%s)",
                           axyne_configuration_name(target->configuration),
                           axyne_architecture_name(target->platform, target->architecture),
                           toolchain_name);
    else
        written = snprintf(buffer, capacity, "%s \xC2\xB7 %s",
                           axyne_configuration_name(target->configuration),
                           axyne_architecture_name(target->platform, target->architecture));
    if (written < 0 || (size_t)written >= capacity) {
        buffer[0] = '\0';
        return AXYNE_STATUS_INVALID_ARGUMENT;
    }
    return AXYNE_STATUS_OK;
}

typedef struct ToolchainEntry {
    const char *name;
    AxyneToolchain toolchain;
} ToolchainEntry;

static const ToolchainEntry toolchain_table[] = {
    { "cl", AXYNE_TOOLCHAIN_MSVC },
    { "clang", AXYNE_TOOLCHAIN_CLANG },
    { "clang++", AXYNE_TOOLCHAIN_CLANG },
    { "gcc", AXYNE_TOOLCHAIN_GCC },
    { "g++", AXYNE_TOOLCHAIN_GCC },
    { "cc", AXYNE_TOOLCHAIN_GCC },
    { "c++", AXYNE_TOOLCHAIN_GCC },
    { "go", AXYNE_TOOLCHAIN_GO },
    { "cargo", AXYNE_TOOLCHAIN_CARGO },
    { "rustc", AXYNE_TOOLCHAIN_RUSTC },
    { "kotlinc", AXYNE_TOOLCHAIN_KOTLINC },
    { "javac", AXYNE_TOOLCHAIN_JAVAC },
    { "java", AXYNE_TOOLCHAIN_JAVA },
    { "swiftc", AXYNE_TOOLCHAIN_SWIFT },
    { "swift", AXYNE_TOOLCHAIN_SWIFT },
    { "python", AXYNE_TOOLCHAIN_PYTHON },
    { "node", AXYNE_TOOLCHAIN_NODE },
    { "tsc", AXYNE_TOOLCHAIN_TSC },
    { "slint-viewer", AXYNE_TOOLCHAIN_SLINT }
};

static AxyneToolchain toolchain_lookup(const char *name)
{
    size_t i;
    for (i = 0; i < sizeof(toolchain_table) / sizeof(toolchain_table[0]); ++i)
        if (strcmp(toolchain_table[i].name, name) == 0)
            return toolchain_table[i].toolchain;
    return AXYNE_TOOLCHAIN_UNKNOWN;
}

AxyneToolchain axyne_toolchain_from_executable(const char *executable)
{
    const char *base, *cursor;
    char name[64];
    size_t length, i;
    AxyneToolchain found;
    if (executable == NULL) return AXYNE_TOOLCHAIN_UNKNOWN;
    base = executable;
    for (cursor = executable; *cursor != '\0'; ++cursor)
        if (*cursor == '/' || *cursor == '\\') base = cursor + 1;
    length = strlen(base);
    if (length == 0 || length >= sizeof(name)) return AXYNE_TOOLCHAIN_UNKNOWN;
    for (i = 0; i < length; ++i)
        name[i] = (char)tolower((unsigned char)base[i]);
    name[length] = '\0';
    {
        static const char *const extensions[] = { ".exe", ".cmd", ".bat" };
        for (i = 0; i < 3; ++i) {
            size_t e = strlen(extensions[i]);
            if (length > e && strcmp(name + length - e, extensions[i]) == 0) {
                length -= e; name[length] = '\0'; break;
            }
        }
    }
    found = toolchain_lookup(name);
    if (found != AXYNE_TOOLCHAIN_UNKNOWN) return found;
    /* Strip a version suffix such as -13, 3.11 or -15.0. */
    while (length > 0 && (isdigit((unsigned char)name[length - 1]) ||
                          name[length - 1] == '.' || name[length - 1] == '-'))
        name[--length] = '\0';
    return length == 0 ? AXYNE_TOOLCHAIN_UNKNOWN : toolchain_lookup(name);
}

const char *axyne_toolchain_name(AxyneToolchain toolchain)
{
    switch (toolchain) {
    case AXYNE_TOOLCHAIN_GCC: return "gcc";
    case AXYNE_TOOLCHAIN_CLANG: return "clang";
    case AXYNE_TOOLCHAIN_MSVC: return "MSVC";
    case AXYNE_TOOLCHAIN_GO: return "Go";
    case AXYNE_TOOLCHAIN_CARGO:
    case AXYNE_TOOLCHAIN_RUSTC: return "Rust";
    case AXYNE_TOOLCHAIN_KOTLINC: return "Kotlin";
    case AXYNE_TOOLCHAIN_JAVAC: return "javac";
    case AXYNE_TOOLCHAIN_JAVA: return "Java";
    case AXYNE_TOOLCHAIN_SWIFT: return "Swift";
    case AXYNE_TOOLCHAIN_PYTHON: return "Python";
    case AXYNE_TOOLCHAIN_NODE: return "Node.js";
    case AXYNE_TOOLCHAIN_TSC: return "TypeScript";
    case AXYNE_TOOLCHAIN_SLINT: return "Slint";
    case AXYNE_TOOLCHAIN_UNKNOWN: break;
    }
    return "";
}

const char *axyne_toolchain_name_for_executable(const char *executable)
{
    return axyne_toolchain_name(axyne_toolchain_from_executable(executable));
}

static void add_argument(AxyneBuildFlags *flags, const char *argument)
{
    flags->arguments[flags->argument_count++] = argument;
}

static void add_environment(AxyneBuildFlags *flags, const char *entry)
{
    flags->environment[flags->environment_count++] = entry;
}

static const char *rust_triple(const AxyneBuildTarget *target)
{
    switch (target->platform) {
    case AXYNE_PLATFORM_WINDOWS:
        return target->architecture == AXYNE_ARCH_X86 ? "i686-pc-windows-msvc"
             : target->architecture == AXYNE_ARCH_ARM64 ? "aarch64-pc-windows-msvc"
             : "x86_64-pc-windows-msvc";
    case AXYNE_PLATFORM_MACOS:
        return target->architecture == AXYNE_ARCH_ARM64 ? "aarch64-apple-darwin"
             : "x86_64-apple-darwin";
    default:
        return target->architecture == AXYNE_ARCH_X86 ? "i686-unknown-linux-gnu"
             : target->architecture == AXYNE_ARCH_ARM64 ? "aarch64-unknown-linux-gnu"
             : "x86_64-unknown-linux-gnu";
    }
}

AxyneStatus axyne_build_target_flags(AxyneToolchain toolchain,
                                     const AxyneBuildTarget *target,
                                     AxyneBuildFlags *flags)
{
    int debug;
    if (flags == NULL) return AXYNE_STATUS_INVALID_ARGUMENT;
    memset(flags, 0, sizeof(*flags));
    if (!axyne_build_target_valid(target)) return AXYNE_STATUS_INVALID_ARGUMENT;
    debug = target->configuration == AXYNE_CONFIGURATION_DEBUG;
    switch (toolchain) {
    case AXYNE_TOOLCHAIN_GCC:
    case AXYNE_TOOLCHAIN_CLANG:
        flags->configuration_applicable = flags->architecture_applicable = 1;
        if (debug) { add_argument(flags, "-g"); add_argument(flags, "-O0"); }
        else add_argument(flags, "-O2");
        if (target->platform == AXYNE_PLATFORM_MACOS) {
            add_argument(flags, "-arch");
            add_argument(flags, target->architecture == AXYNE_ARCH_ARM64 ? "arm64" : "x86_64");
        } else if (target->architecture == AXYNE_ARCH_X64) {
            add_argument(flags, "-m64");
        } else if (target->architecture == AXYNE_ARCH_X86) {
            add_argument(flags, "-m32");
        }
        break;
    case AXYNE_TOOLCHAIN_MSVC:
        flags->configuration_applicable = flags->architecture_applicable = 1;
        if (debug) { add_argument(flags, "/Zi"); add_argument(flags, "/Od"); }
        else add_argument(flags, "/O2");
        break;
    case AXYNE_TOOLCHAIN_GO:
        flags->configuration_applicable = flags->architecture_applicable = 1;
        add_environment(flags, target->platform == AXYNE_PLATFORM_WINDOWS ? "GOOS=windows"
                               : target->platform == AXYNE_PLATFORM_MACOS ? "GOOS=darwin"
                               : "GOOS=linux");
        add_environment(flags, target->architecture == AXYNE_ARCH_X86 ? "GOARCH=386"
                               : target->architecture == AXYNE_ARCH_ARM64 ? "GOARCH=arm64"
                               : "GOARCH=amd64");
        if (debug) add_argument(flags, "-gcflags=all=-N -l");
        break;
    case AXYNE_TOOLCHAIN_CARGO:
        flags->configuration_applicable = flags->architecture_applicable = 1;
        if (!debug) add_argument(flags, "--release");
        add_argument(flags, "--target");
        add_argument(flags, rust_triple(target));
        break;
    case AXYNE_TOOLCHAIN_RUSTC:
        flags->configuration_applicable = flags->architecture_applicable = 1;
        if (debug) { add_argument(flags, "-Copt-level=0"); add_argument(flags, "-Cdebuginfo=2"); }
        else add_argument(flags, "-Copt-level=3");
        add_argument(flags, "--target");
        add_argument(flags, rust_triple(target));
        break;
    case AXYNE_TOOLCHAIN_SWIFT:
        flags->configuration_applicable = flags->architecture_applicable = 1;
        if (debug) { add_argument(flags, "-Onone"); add_argument(flags, "-g"); }
        else add_argument(flags, "-O");
        if (target->platform == AXYNE_PLATFORM_MACOS) {
            add_argument(flags, "-target");
            add_argument(flags, target->architecture == AXYNE_ARCH_ARM64
                                    ? "arm64-apple-macosx" : "x86_64-apple-macosx");
        }
        break;
    case AXYNE_TOOLCHAIN_JAVAC:
        flags->configuration_applicable = 1;
        add_argument(flags, debug ? "-g" : "-g:none");
        break;
    default:
        break; /* python, node, tsc, slint, kotlinc, java, unknown: no effect */
    }
    return AXYNE_STATUS_OK;
}
