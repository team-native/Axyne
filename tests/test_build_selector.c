#include <stdio.h>
#include <string.h>

#include "axyne/build_selector.h"

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return 0; \
        } \
    } while (0)

static AxyneBuildTarget target_for(AxynePlatform platform, AxyneArchitecture arch)
{
    AxyneBuildTarget target;
    target.platform = platform;
    target.configuration = AXYNE_CONFIGURATION_DEBUG;
    target.architecture = arch;
    return target;
}

static int test_entries(void)
{
    AxyneBuildSelectorEntry entries[AXYNE_BUILD_SELECTOR_MAX_ENTRIES];
    AxyneBuildTarget target = target_for(AXYNE_PLATFORM_MACOS, AXYNE_ARCH_ARM64);
    size_t count;

    count = axyne_build_selector_entries(AXYNE_LANGUAGE_C, &target, entries);
    CHECK(count == 4);
    CHECK(entries[0].group == AXYNE_BUILD_SELECTOR_CONFIGURATION && entries[0].checked);
    CHECK(strcmp(entries[0].title, "Debug") == 0 && !entries[1].checked);
    CHECK(entries[2].group == AXYNE_BUILD_SELECTOR_ARCHITECTURE);
    CHECK(strcmp(entries[2].title, "arm64") == 0 && entries[2].checked);
    CHECK(strcmp(entries[3].title, "x86_64") == 0 && !entries[3].checked);

    count = axyne_build_selector_entries(AXYNE_LANGUAGE_JAVA, &target, entries);
    CHECK(count == 2 && entries[1].group == AXYNE_BUILD_SELECTOR_CONFIGURATION);
    CHECK(axyne_build_selector_entries(AXYNE_LANGUAGE_PYTHON, &target, entries) == 0);
    CHECK(axyne_build_selector_entries(AXYNE_LANGUAGE_KOTLIN, &target, entries) == 0);
    CHECK(axyne_build_selector_entries(AXYNE_LANGUAGE_NONE, &target, entries) == 4);

    target = target_for(AXYNE_PLATFORM_WINDOWS, AXYNE_ARCH_X64);
    target.configuration = AXYNE_CONFIGURATION_RELEASE;
    count = axyne_build_selector_entries(AXYNE_LANGUAGE_CPP, &target, entries);
    CHECK(count == 5 && !entries[0].checked && entries[1].checked);
    CHECK(strcmp(entries[2].title, "x64") == 0 && entries[2].checked);
    CHECK(strcmp(entries[3].title, "x86") == 0 && strcmp(entries[4].title, "arm64") == 0);
    CHECK(axyne_build_selector_entries(AXYNE_LANGUAGE_C, NULL, entries) == 0);
    return 1;
}

static int test_apply(void)
{
    AxyneBuildTarget target = target_for(AXYNE_PLATFORM_MACOS, AXYNE_ARCH_ARM64);
    CHECK(axyne_build_selector_apply(&target, AXYNE_BUILD_SELECTOR_CONFIGURATION,
                                     AXYNE_CONFIGURATION_RELEASE) == 1);
    CHECK(target.configuration == AXYNE_CONFIGURATION_RELEASE);
    CHECK(axyne_build_selector_apply(&target, AXYNE_BUILD_SELECTOR_CONFIGURATION,
                                     AXYNE_CONFIGURATION_RELEASE) == 0);
    CHECK(axyne_build_selector_apply(&target, AXYNE_BUILD_SELECTOR_CONFIGURATION, 9) == 0);
    CHECK(axyne_build_selector_apply(&target, AXYNE_BUILD_SELECTOR_ARCHITECTURE,
                                     AXYNE_ARCH_X64) == 1);
    CHECK(target.architecture == AXYNE_ARCH_X64);
    /* x86 is not offered on macOS. */
    CHECK(axyne_build_selector_apply(&target, AXYNE_BUILD_SELECTOR_ARCHITECTURE,
                                     AXYNE_ARCH_X86) == 0);
    CHECK(target.architecture == AXYNE_ARCH_X64);
    CHECK(axyne_build_selector_apply(NULL, AXYNE_BUILD_SELECTOR_ARCHITECTURE, 0) == 0);
    return 1;
}

static int test_label(void)
{
    AxyneBuildTarget target = target_for(AXYNE_PLATFORM_MACOS, AXYNE_ARCH_ARM64);
    AxyneRuntime items[2];
    AxyneRuntimeList list = { items, 0 };
    char label[96];

    /* Not discovered yet: no parentheses. */
    CHECK(axyne_build_selector_label(AXYNE_LANGUAGE_C, NULL, &target, NULL,
                                     label, sizeof(label)) == AXYNE_STATUS_OK);
    CHECK(strcmp(label, "Debug \xC2\xB7 arm64") == 0);

    /* Discovered, but this language has no runtime. */
    CHECK(axyne_build_selector_label(AXYNE_LANGUAGE_C, &list, &target, NULL,
                                     label, sizeof(label)) == AXYNE_STATUS_OK);
    CHECK(strcmp(label, "Debug \xC2\xB7 arm64") == 0);

    memset(items, 0, sizeof(items));
    items[0].kind = AXYNE_RUNTIME_C;
    items[0].executable = (char *)"/usr/bin/clang";
    items[0].version = (char *)"1";
    list.count = 1;
    CHECK(axyne_build_selector_label(AXYNE_LANGUAGE_C, &list, &target, "/w/a.c",
                                     label, sizeof(label)) == AXYNE_STATUS_OK);
    CHECK(strcmp(label, "Debug \xC2\xB7 arm64 (clang)") == 0);
    target.configuration = AXYNE_CONFIGURATION_RELEASE;
    CHECK(axyne_build_selector_label(AXYNE_LANGUAGE_C, &list, &target, NULL,
                                     label, sizeof(label)) == AXYNE_STATUS_OK);
    CHECK(strcmp(label, "Release \xC2\xB7 arm64 (clang)") == 0);
    CHECK(axyne_build_selector_label(AXYNE_LANGUAGE_NONE, &list, &target, NULL,
                                     label, sizeof(label)) == AXYNE_STATUS_OK);
    CHECK(strcmp(label, "Release \xC2\xB7 arm64") == 0);
    return 1;
}

int axyne_test_build_selector(const char *root)
{
    (void)root;
    return test_entries() && test_apply() && test_label();
}
