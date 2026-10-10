#include "test_support.h"

#include <stdlib.h>
#include <string.h>

#include "axyne/memory_usage.h"

int axyne_test_memory_usage(const char *root)
{
    AxyneMemoryUsage first, second;
    AxyneError error = {0};
    char buffer[64];
    unsigned char *block;
    (void)root;
    (void)axyne_test_path;
    (void)axyne_test_make_directory;

    AXYNE_TEST_STATUS(axyne_memory_usage_sample(&first, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(first.resident_bytes > 0);
    AXYNE_TEST_CHECK(first.peak_resident_bytes >= first.resident_bytes);
    AXYNE_TEST_STATUS(axyne_memory_usage_sample(NULL, &error), AXYNE_STATUS_INVALID_ARGUMENT);

    /* Touching 16 MB raises the peak at least that far. */
    block = (unsigned char *)malloc(16u * 1024u * 1024u);
    AXYNE_TEST_CHECK(block != NULL);
    for (size_t i = 0; i < 16u * 1024u * 1024u; i += 4096) block[i] = (unsigned char)i;
    AXYNE_TEST_STATUS(axyne_memory_usage_sample(&second, &error), AXYNE_STATUS_OK);
    AXYNE_TEST_CHECK(second.peak_resident_bytes >= first.peak_resident_bytes);
    AXYNE_TEST_CHECK(second.peak_resident_bytes >= second.resident_bytes);
    AXYNE_TEST_CHECK(second.resident_bytes + 4u * 1024u * 1024u > first.resident_bytes);
    free(block);

    /* Gauge text and thresholds (D15). */
    (void)axyne_memory_format_gauge((uint64_t)(38.4 * 1048576.0), AXYNE_MEMORY_BUDGET_BYTES,
                                    buffer, sizeof(buffer));
    AXYNE_TEST_STREQ(buffer, "38.4 / 100 MB");
    (void)axyne_memory_format_gauge(0, AXYNE_MEMORY_BUDGET_BYTES, buffer, sizeof(buffer));
    AXYNE_TEST_STREQ(buffer, "0.0 / 100 MB");
    (void)axyne_memory_format_gauge(AXYNE_MEMORY_MB * 3 / 2, AXYNE_MEMORY_MB * 5 / 2,
                                    buffer, sizeof(buffer));
    AXYNE_TEST_STREQ(buffer, "1.5 / 2.5 MB");
    (void)axyne_memory_format_bytes(AXYNE_MEMORY_MB * 1234, buffer, sizeof(buffer));
    AXYNE_TEST_STREQ(buffer, "1234.0 MB");
    AXYNE_TEST_EQ_INT(axyne_memory_format_gauge(AXYNE_MEMORY_MB, AXYNE_MEMORY_BUDGET_BYTES,
                                                buffer, 4), 12);
    AXYNE_TEST_STREQ(buffer, "1.0");
    AXYNE_TEST_EQ_INT(axyne_memory_format_bytes(AXYNE_MEMORY_MB, NULL, 0), 6);
    AXYNE_TEST_EQ_INT(axyne_memory_usage_level(80 * AXYNE_MEMORY_MB), AXYNE_MEMORY_LEVEL_NORMAL);
    AXYNE_TEST_EQ_INT(axyne_memory_usage_level(80 * AXYNE_MEMORY_MB + 1), AXYNE_MEMORY_LEVEL_WARNING);
    AXYNE_TEST_EQ_INT(axyne_memory_usage_level(100 * AXYNE_MEMORY_MB), AXYNE_MEMORY_LEVEL_WARNING);
    AXYNE_TEST_EQ_INT(axyne_memory_usage_level(100 * AXYNE_MEMORY_MB + 1), AXYNE_MEMORY_LEVEL_ERROR);
    return 1;
}
