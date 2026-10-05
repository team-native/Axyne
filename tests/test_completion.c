#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axyne/completion.h"

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return 0; \
        } \
    } while (0)

/* Builds the list for the text before the first '|' marker (the caret). */
static char *build_at_marker(const char *source, const char *const *sets,
                             size_t set_count, size_t *prefix)
{
    char text[8192];
    const char *bar = strchr(source, '|');
    size_t before = (size_t)(bar - source);
    size_t after = strlen(bar + 1);
    memcpy(text, source, before);
    memcpy(text + before, bar + 1, after + 1);
    return axyne_completion_build(text, before + after, before, sets,
                                  set_count, prefix);
}

static int test_prefix(void)
{
    CHECK(axyne_completion_prefix_length("int foo", 7, 7) == 3);
    CHECK(axyne_completion_prefix_length("int foo ", 8, 8) == 0);
    CHECK(axyne_completion_prefix_length("a.bc", 4, 4) == 2);
    CHECK(axyne_completion_prefix_length("abc", 3, 9) == 0);
    CHECK(axyne_completion_prefix_length(NULL, 0, 0) == 0);
    return 1;
}

static int test_document_words(void)
{
    size_t prefix = 0;
    char *list = build_at_marker(
        "counter count_total Counter other\ncou|", NULL, 0, &prefix);
    CHECK(list != NULL);
    CHECK(prefix == 3);
    /* case-insensitive order, duplicates of case kept distinct */
    CHECK(strcmp(list, "count_total Counter counter") == 0);
    free(list);

    /* the word being typed is not offered; other words match */
    list = build_at_marker("value valuable valu|", NULL, 0, &prefix);
    CHECK(list != NULL && strcmp(list, "valuable value") == 0);
    free(list);
    list = build_at_marker("only|", NULL, 0, &prefix);
    CHECK(list == NULL);

    /* typing in the middle of a word uses the text before the caret */
    list = build_at_marker("alpha_beta alp|ha", NULL, 0, &prefix);
    CHECK(list != NULL && strcmp(list, "alpha_beta") == 0 && prefix == 3);
    free(list);

    /* duplicates collapse */
    list = build_at_marker("foobar foobar foobar fo|", NULL, 0, &prefix);
    CHECK(list != NULL && strcmp(list, "foobar") == 0);
    free(list);
    return 1;
}

static int test_prefix_rules(void)
{
    size_t prefix = 7;
    CHECK(build_at_marker("abc a|", NULL, 0, &prefix) == NULL);
    CHECK(prefix == 0);
    CHECK(build_at_marker("abc \n|", NULL, 0, &prefix) == NULL);
    CHECK(build_at_marker("123456 12|", NULL, 0, &prefix) == NULL); /* digit start */
    CHECK(build_at_marker("ab ab|", NULL, 0, &prefix) == NULL);     /* only itself */
    CHECK(axyne_completion_build(NULL, 0, 0, NULL, 0, NULL) == NULL);
    CHECK(axyne_completion_build("ab", 2, 5, NULL, 0, NULL) == NULL);
    return 1;
}

static int test_keywords(void)
{
    const char *sets[] = {"while whole void volatile", NULL,
                          "#define whi-le Whale"};
    size_t prefix = 0;
    char *list = build_at_marker("whisper wh|", sets, 3, &prefix);
    CHECK(list != NULL);
    /* keywords merge with words; tokens that are not identifiers are ignored */
    CHECK(strcmp(list, "Whale while whisper whole") == 0);
    free(list);
    list = build_at_marker("x vo|", sets, 3, &prefix);
    CHECK(list != NULL && strcmp(list, "void volatile") == 0);
    free(list);
    /* a keyword already in the document is listed once */
    list = build_at_marker("void vo|", sets, 3, &prefix);
    CHECK(list != NULL && strcmp(list, "void volatile") == 0);
    free(list);
    CHECK(build_at_marker("qq|", sets, 3, &prefix) == NULL);
    return 1;
}

static int test_limit(void)
{
    size_t size = 400 * 12;
    char *text = (char *)malloc(size + 8);
    char *list;
    size_t i, items = 0, prefix = 0;
    int n;
    CHECK(text != NULL);
    text[0] = '\0';
    for (i = 0; i < 400; ++i) {
        n = snprintf(text + strlen(text), 16, "item%03u ", (unsigned)i);
        CHECK(n > 0);
    }
    strcat(text, "it");
    list = axyne_completion_build(text, strlen(text), strlen(text), NULL, 0,
                                  &prefix);
    CHECK(list != NULL && prefix == 2);
    for (i = 0; list[i] != '\0'; ++i)
        if (list[i] == ' ') ++items;
    CHECK(items + 1 == AXYNE_COMPLETION_MAX_ITEMS);
    CHECK(strncmp(list, "item000 item001", 15) == 0);
    free(list);
    free(text);
    return 1;
}

static int test_utf8(void)
{
    size_t prefix = 0;
    char *list = build_at_marker(
        "\xed\x95\x9c\xea\xb8\x80\xec\x9d\xb4\xeb\xa6\x84 \xed\x95\x9c\xea\xb8\x80|",
        NULL, 0, &prefix);
    CHECK(list != NULL && prefix == 6); /* two characters, six bytes */
    CHECK(strcmp(list, "\xed\x95\x9c\xea\xb8\x80\xec\x9d\xb4\xeb\xa6\x84") == 0);
    free(list);
    /* one multi-byte character is below the two-character minimum */
    CHECK(build_at_marker("\xed\x95\x9c\xea\xb8\x80 \xed\x95\x9c|", NULL, 0,
                          &prefix) == NULL);
    /* a stray continuation byte and invalid UTF-8 must not crash */
    list = build_at_marker("a\x80\x80 \xff\xfe ab\x80|", NULL, 0, &prefix);
    free(list);
    return 1;
}

static int test_large_document(void)
{
    size_t length = AXYNE_COMPLETION_MAX_SCAN * 2;
    char *text = (char *)malloc(length + 16);
    char *list;
    size_t prefix = 0;
    CHECK(text != NULL);
    memset(text, ' ', length);
    memcpy(text, "far_away", 8);                        /* outside the scan window */
    memcpy(text + length / 2 - 40, "near_word ne", 12); /* caret after "ne" */
    list = axyne_completion_build(text, length, length / 2 - 28, NULL, 0, &prefix);
    CHECK(list != NULL && strcmp(list, "near_word") == 0);
    free(list);
    memcpy(text + 8, " fa", 3);
    list = axyne_completion_build(text, length, 11, NULL, 0, &prefix);
    CHECK(list != NULL && strcmp(list, "far_away") == 0);
    free(list);
    free(text);
    return 1;
}

int axyne_test_completion(const char *root)
{
    (void)root;
    CHECK(test_prefix());
    CHECK(test_document_words());
    CHECK(test_prefix_rules());
    CHECK(test_keywords());
    CHECK(test_limit());
    CHECK(test_utf8());
    CHECK(test_large_document());
    return 1;
}
