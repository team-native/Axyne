#ifndef AXYNE_BUILD_TARGET_H
#define AXYNE_BUILD_TARGET_H

#include <stddef.h>

#include "axyne/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build target model: Configuration x Architecture, limited by operating
 * system. Pure data and string mapping; nothing here starts a process. */

typedef enum AxynePlatform {
    AXYNE_PLATFORM_WINDOWS = 0,
    AXYNE_PLATFORM_MACOS,
    AXYNE_PLATFORM_OTHER /* Linux and other POSIX hosts (tests, tooling) */
} AxynePlatform;

typedef enum AxyneConfiguration {
    AXYNE_CONFIGURATION_DEBUG = 0,
    AXYNE_CONFIGURATION_RELEASE
} AxyneConfiguration;

typedef enum AxyneArchitecture {
    AXYNE_ARCH_X64 = 0, /* shown as "x64" on Windows, "x86_64" on macOS */
    AXYNE_ARCH_X86,
    AXYNE_ARCH_ARM64
} AxyneArchitecture;

#define AXYNE_ARCH_MAX_OPTIONS 3

typedef struct AxyneBuildTarget {
    AxynePlatform platform;
    AxyneConfiguration configuration;
    AxyneArchitecture architecture;
} AxyneBuildTarget;

/* Toolchain classified from a detected executable path. */
typedef enum AxyneToolchain {
    AXYNE_TOOLCHAIN_UNKNOWN = 0,
    AXYNE_TOOLCHAIN_GCC,
    AXYNE_TOOLCHAIN_CLANG,
    AXYNE_TOOLCHAIN_MSVC,
    AXYNE_TOOLCHAIN_GO,
    AXYNE_TOOLCHAIN_CARGO,
    AXYNE_TOOLCHAIN_RUSTC,
    AXYNE_TOOLCHAIN_KOTLINC,
    AXYNE_TOOLCHAIN_JAVAC,
    AXYNE_TOOLCHAIN_JAVA,
    AXYNE_TOOLCHAIN_SWIFT, /* swiftc and swift */
    AXYNE_TOOLCHAIN_PYTHON,
    AXYNE_TOOLCHAIN_NODE,
    AXYNE_TOOLCHAIN_TSC,
    AXYNE_TOOLCHAIN_SLINT
} AxyneToolchain;

AxynePlatform axyne_build_target_host_platform(void);
/* Architecture this binary was compiled for (compile-time macros). */
AxyneArchitecture axyne_build_target_host_architecture(void);

/* Fills out[] with the architectures offered on a platform, in menu order,
 * and returns their count (at most AXYNE_ARCH_MAX_OPTIONS).
 *   Windows: x64, x86, arm64      macOS: arm64, x86_64
 *   other:   x64, arm64 */
size_t axyne_build_target_architectures(AxynePlatform platform,
                                        AxyneArchitecture out[AXYNE_ARCH_MAX_OPTIONS]);
int axyne_build_target_architecture_allowed(AxynePlatform platform,
                                            AxyneArchitecture architecture);
/* "x64"/"x86"/"arm64" on Windows and other; "x86_64"/"arm64" on macOS. */
const char *axyne_architecture_name(AxynePlatform platform,
                                    AxyneArchitecture architecture);
const char *axyne_configuration_name(AxyneConfiguration configuration);

/* Debug and the host architecture; when the host architecture is not offered
 * on the platform the first offered one is used. The _for variant is pure. */
AxyneBuildTarget axyne_build_target_default(void);
AxyneBuildTarget axyne_build_target_default_for(AxynePlatform platform,
                                                AxyneArchitecture host);

/* 1 when the configuration and architecture are valid for target->platform. */
int axyne_build_target_valid(const AxyneBuildTarget *target);

/* Writes "Debug \xC2\xB7 x64 (MSVC)", or "Debug \xC2\xB7 arm64" when
 * toolchain_name is NULL or empty. */
AxyneStatus axyne_build_target_label(const AxyneBuildTarget *target,
                                     const char *toolchain_name,
                                     char *buffer, size_t capacity);

/* Classifies an executable path by base name: case-insensitive, extension
 * (.exe .cmd .bat) and version suffix (gcc-13, python3.11) ignored. */
AxyneToolchain axyne_toolchain_from_executable(const char *executable);
/* Display name: cl -> MSVC, clang/clang++ -> clang, gcc/g++/cc/c++ -> gcc,
 * go -> Go, cargo/rustc -> Rust, kotlinc -> Kotlin, javac -> javac,
 * java -> Java, swiftc/swift -> Swift, python -> Python, node -> Node.js,
 * tsc -> TypeScript, slint-viewer -> Slint; "" when unknown. */
const char *axyne_toolchain_name(AxyneToolchain toolchain);
const char *axyne_toolchain_name_for_executable(const char *executable);

#define AXYNE_BUILD_FLAGS_MAX_ARGUMENTS 6
#define AXYNE_BUILD_FLAGS_MAX_ENVIRONMENT 2

/* Per-toolchain mapping of a target. All strings are static literals. */
typedef struct AxyneBuildFlags {
    const char *arguments[AXYNE_BUILD_FLAGS_MAX_ARGUMENTS];
    size_t argument_count;
    const char *environment[AXYNE_BUILD_FLAGS_MAX_ENVIRONMENT]; /* KEY=VALUE */
    size_t environment_count;
    /* Whether Debug/Release changes anything for this toolchain. */
    int configuration_applicable;
    /* Whether the architecture selects anything. When 0 the UI hides the
     * architecture group (python, node, tsc, slint, kotlinc, java/javac). */
    int architecture_applicable;
} AxyneBuildFlags;

/* Mapping (Debug / Release):
 *   gcc, clang  -g -O0 / -O2; macOS -arch arm64|x86_64, else -m64|-m32
 *               (arm64 elsewhere: no flag)
 *   MSVC cl     /Zi /Od / /O2; architecture recorded only (cl comes from the
 *               Developer Prompt environment), no argument
 *   go          env GOOS/GOARCH; Debug -gcflags=all=-N -l
 *   cargo       [--release] --target <triple>
 *   rustc       -Copt-level=0 -Cdebuginfo=2 / -Copt-level=3, --target <triple>
 *   swift       -Onone -g / -O; macOS -target <arch>-apple-macosx
 *   javac       -g / -g:none (architecture not applicable)
 *   others      no effect */
AxyneStatus axyne_build_target_flags(AxyneToolchain toolchain,
                                     const AxyneBuildTarget *target,
                                     AxyneBuildFlags *flags);

#ifdef __cplusplus
}
#endif

#endif
