#include "axyne/completion.h"

#include <stdlib.h>
#include <string.h>

#define AXYNE_COMPLETION_MAX_CANDIDATES 50000

typedef struct Candidate {
    const char *text;
    size_t length;
} Candidate;

typedef struct CandidateList {
    Candidate *items;
    size_t count;
    size_t capacity;
    int failed;
} CandidateList;

static int is_word_byte(unsigned char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') || c == '_' || c >= 0x80;
}

static int is_start_byte(unsigned char c)
{
    return is_word_byte(c) && !(c >= '0' && c <= '9');
}

static unsigned char fold(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') ? (unsigned char)(c - 'A' + 'a') : c;
}

static int compare_fold(const char *a, size_t a_length, const char *b,
                        size_t b_length)
{
    size_t n = a_length < b_length ? a_length : b_length;
    size_t i;
    for (i = 0; i < n; ++i) {
        unsigned char x = fold((unsigned char)a[i]);
        unsigned char y = fold((unsigned char)b[i]);
        if (x != y) return x < y ? -1 : 1;
    }
    if (a_length != b_length) return a_length < b_length ? -1 : 1;
    return 0;
}

static int compare_candidates(const void *left, const void *right)
{
    const Candidate *a = (const Candidate *)left;
    const Candidate *b = (const Candidate *)right;
    int order = compare_fold(a->text, a->length, b->text, b->length);
    if (order != 0) return order;
    return memcmp(a->text, b->text, a->length); /* equal lengths here */
}

static int has_prefix(const char *word, size_t length, const char *prefix,
                      size_t prefix_length)
{
    size_t i;
    if (length <= prefix_length) return 0;
    for (i = 0; i < prefix_length; ++i)
        if (fold((unsigned char)word[i]) != fold((unsigned char)prefix[i]))
            return 0;
    return 1;
}

static void add_candidate(CandidateList *list, const char *word, size_t length)
{
    if (list->failed || list->count >= AXYNE_COMPLETION_MAX_CANDIDATES) return;
    if (list->count == list->capacity) {
        size_t capacity = list->capacity == 0 ? 256 : list->capacity * 2;
        Candidate *grown = (Candidate *)realloc(list->items,
                                                capacity * sizeof(Candidate));
        if (grown == NULL) { list->failed = 1; return; }
        list->items = grown;
        list->capacity = capacity;
    }
    list->items[list->count].text = word;
    list->items[list->count].length = length;
    ++list->count;
}

size_t axyne_completion_prefix_length(const char *text, size_t length,
                                      size_t caret)
{
    size_t start;
    if (text == NULL || caret > length) return 0;
    start = caret;
    while (start > 0 && is_word_byte((unsigned char)text[start - 1])) --start;
    return caret - start;
}

/* Counts characters (not UTF-8 continuation bytes) in a prefix. */
static size_t prefix_characters(const char *prefix, size_t length)
{
    size_t i, count = 0;
    for (i = 0; i < length; ++i)
        if (((unsigned char)prefix[i] & 0xc0) != 0x80) ++count;
    return count;
}

static void scan_words(CandidateList *list, const char *text, size_t begin,
                       size_t end, size_t caret_word_start,
                       const char *prefix, size_t prefix_length)
{
    size_t i = begin;
    while (i < end) {
        size_t start;
        if (!is_word_byte((unsigned char)text[i])) { ++i; continue; }
        start = i;
        while (i < end && is_word_byte((unsigned char)text[i])) ++i;
        if (start == caret_word_start) continue; /* the word being typed */
        if (!is_start_byte((unsigned char)text[start])) continue;
        if (i - start > AXYNE_COMPLETION_MAX_WORD) continue;
        if (has_prefix(text + start, i - start, prefix, prefix_length))
            add_candidate(list, text + start, i - start);
    }
}

static void scan_keywords(CandidateList *list, const char *keywords,
                          const char *prefix, size_t prefix_length)
{
    const char *p = keywords;
    if (keywords == NULL) return;
    while (*p != '\0') {
        const char *start;
        size_t length = 0;
        int valid = 1;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
        start = p;
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' &&
               *p != '\n') {
            if (!is_word_byte((unsigned char)*p)) valid = 0;
            ++p;
            ++length;
        }
        if (length == 0 || !valid || !is_start_byte((unsigned char)*start))
            continue;
        if (length > AXYNE_COMPLETION_MAX_WORD) continue;
        if (has_prefix(start, length, prefix, prefix_length))
            add_candidate(list, start, length);
    }
}

char *axyne_completion_build(const char *text, size_t length, size_t caret,
                             const char *const *keyword_sets,
                             size_t keyword_set_count, size_t *prefix_bytes)
{
    CandidateList list = {NULL, 0, 0, 0};
    size_t prefix_length;
    size_t caret_word_start;
    size_t begin = 0;
    size_t end = length;
    size_t i, kept = 0, total = 0;
    const char *prefix;
    char *result = NULL;
    char *out;

    if (prefix_bytes != NULL) *prefix_bytes = 0;
    if (text == NULL || caret > length) return NULL;
    prefix_length = axyne_completion_prefix_length(text, length, caret);
    if (prefix_length == 0 ||
        prefix_characters(text + caret - prefix_length, prefix_length) <
            AXYNE_COMPLETION_MIN_PREFIX)
        return NULL;
    prefix = text + caret - prefix_length;
    if (!is_start_byte((unsigned char)prefix[0])) return NULL;
    if (prefix_length > AXYNE_COMPLETION_MAX_WORD) return NULL;
    caret_word_start = caret - prefix_length;

    if (length > AXYNE_COMPLETION_MAX_SCAN) {
        size_t half = AXYNE_COMPLETION_MAX_SCAN / 2;
        begin = caret > half ? caret - half : 0;
        end = begin + AXYNE_COMPLETION_MAX_SCAN;
        if (end > length) { end = length; begin = length - AXYNE_COMPLETION_MAX_SCAN; }
        /* Start on a word boundary so a cut word is not suggested. */
        while (begin > 0 && begin < caret_word_start &&
               is_word_byte((unsigned char)text[begin - 1]) &&
               is_word_byte((unsigned char)text[begin]))
            ++begin;
        while (end < length && end > caret &&
               is_word_byte((unsigned char)text[end - 1]) &&
               is_word_byte((unsigned char)text[end]))
            --end;
    }
    scan_words(&list, text, begin, end, caret_word_start, prefix, prefix_length);
    for (i = 0; keyword_sets != NULL && i < keyword_set_count; ++i)
        scan_keywords(&list, keyword_sets[i], prefix, prefix_length);
    if (list.failed || list.count == 0) { free(list.items); return NULL; }

    qsort(list.items, list.count, sizeof(Candidate), compare_candidates);
    for (i = 0; i < list.count; ++i) {
        if (kept > 0 && list.items[kept - 1].length == list.items[i].length &&
            memcmp(list.items[kept - 1].text, list.items[i].text,
                   list.items[i].length) == 0)
            continue;
        list.items[kept++] = list.items[i];
        if (kept == AXYNE_COMPLETION_MAX_ITEMS) break;
    }
    for (i = 0; i < kept; ++i) total += list.items[i].length + 1;
    result = (char *)malloc(total);
    if (result != NULL) {
        out = result;
        for (i = 0; i < kept; ++i) {
            if (i > 0) *out++ = ' ';
            memcpy(out, list.items[i].text, list.items[i].length);
            out += list.items[i].length;
        }
        *out = '\0';
        if (prefix_bytes != NULL) *prefix_bytes = prefix_length;
    }
    free(list.items);
    return result;
}
