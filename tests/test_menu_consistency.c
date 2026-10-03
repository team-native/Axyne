/* Static consistency checks between the menu definitions and their handlers.
 * The native adapters cannot be built or run on every platform, so this reads
 * both adapter sources as text and verifies, for each menu entry:
 *   macOS   - every action selector installed in axyne_install_menu has an
 *             implementation in AxyneWorkspaceView (or is a system selector);
 *   Windows - every command id passed to axyne_menu_add is unique, stays out
 *             of the Open Recent id range, and is handled by WM_COMMAND or
 *             axyne_action_command;
 *   both    - the shared menu entries exist on both platforms. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "axyne/document.h"

#ifndef AXYNE_SOURCE_DIR
#error "AXYNE_SOURCE_DIR must point at the repository root"
#endif

static int failures;
#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
    ++failures; } } while (0)
#define FAIL(...) do { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); \
    ++failures; } while (0)

static char *read_file(const char *relative)
{
    char path[1024];
    FILE *file;
    long size;
    char *text;
    (void)snprintf(path, sizeof(path), "%s/%s", AXYNE_SOURCE_DIR, relative);
    file = fopen(path, "rb");
    if (file == NULL) { FAIL("cannot open %s", path); return NULL; }
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    text = (char *)malloc((size_t)size + 1);
    if (text == NULL || fread(text, 1, (size_t)size, file) != (size_t)size) {
        fclose(file); free(text); FAIL("cannot read %s", path); return NULL;
    }
    text[size] = '\0';
    fclose(file);
    return text;
}

/* Copy of text[from_marker, to_marker); the last occurrence of from_marker is
 * used so forward declarations are skipped. */
static char *region(const char *text, const char *from_marker, const char *to_marker)
{
    const char *begin = NULL;
    const char *cursor = text;
    const char *end;
    char *copy;
    size_t length;
    while ((cursor = strstr(cursor, from_marker)) != NULL) { begin = cursor; ++cursor; }
    if (begin == NULL) { FAIL("missing marker: %s", from_marker); return NULL; }
    end = strstr(begin, to_marker);
    if (end == NULL) { FAIL("missing marker: %s", to_marker); return NULL; }
    length = (size_t)(end - begin);
    copy = (char *)malloc(length + 1);
    if (copy == NULL) return NULL;
    memcpy(copy, begin, length);
    copy[length] = '\0';
    return copy;
}

static int is_identifier_char(int c) { return isalnum(c) || c == '_'; }

/* True when `word` occurs in text as a whole identifier. */
static int has_word(const char *text, const char *word)
{
    size_t length = strlen(word);
    const char *cursor = text;
    while ((cursor = strstr(cursor, word)) != NULL) {
        int before = cursor == text || !is_identifier_char((unsigned char)cursor[-1]);
        int after = !is_identifier_char((unsigned char)cursor[length]);
        if (before && after) return 1;
        cursor += length;
    }
    return 0;
}

/* ---- macOS: selectors -------------------------------------------------- */

static int selector_is_system(const char *name)
{
    return strcmp(name, "orderFrontStandardAboutPanel:") == 0 ||
           strcmp(name, "terminate:") == 0;
}

static void check_macos_selectors(void)
{
    char *source = read_file("src/features/ui/platform/macos/ui_macos.m");
    char *install, *implementation;
    const char *cursor;
    size_t count = 0;
    if (source == NULL) return;
    install = region(source, "static void axyne_install_menu(NSApplication *application,\n"
                             "                               AxyneWorkspaceView *workspace)\n{",
                     "int axyne_ui_run");
    implementation = region(source, "@implementation AxyneWorkspaceView",
                            "@interface AxyneApplicationDelegate");
    if (install != NULL && implementation != NULL) {
        cursor = install;
        while ((cursor = strstr(cursor, "@selector(")) != NULL) {
            char name[96];
            char signature[128];
            const char *close;
            size_t length;
            cursor += strlen("@selector(");
            close = strchr(cursor, ')');
            if (close == NULL) break;
            length = (size_t)(close - cursor);
            if (length == 0 || length >= sizeof(name)) { FAIL("bad selector text"); break; }
            memcpy(name, cursor, length);
            name[length] = '\0';
            ++count;
            if (selector_is_system(name)) continue;
            if (name[length - 1] != ':') { FAIL("menu selector %s takes no sender", name); continue; }
            (void)snprintf(signature, sizeof(signature), "\n- (void)%s(", name);
            if (strstr(implementation, signature) == NULL)
                FAIL("macOS menu selector %s has no implementation", name);
        }
        CHECK(count >= 40);
    }
    free(install); free(implementation); free(source);
}

/* ---- Windows: command ids ---------------------------------------------- */

typedef struct { char name[64]; long value; } EnumEntry;
static EnumEntry entries[256];
static size_t entry_count;

static void strip_comments(char *text)
{
    char *in = text, *out = text;
    while (*in != '\0') {
        if (in[0] == '/' && in[1] == '*') {
            in += 2;
            while (*in != '\0' && !(in[0] == '*' && in[1] == '/')) ++in;
            if (*in != '\0') in += 2;
        } else if (in[0] == '/' && in[1] == '/') {
            while (*in != '\0' && *in != '\n') ++in;
        } else *out++ = *in++;
    }
    *out = '\0';
}

/* Parses `enum { A = 1, B, ... };` blocks made only of plain names and numbers
 * (the command id enums); other enums are skipped. */
static void parse_enums(const char *source)
{
    const char *cursor = source;
    while ((cursor = strstr(cursor, "enum {")) != NULL) {
        const char *end = strstr(cursor, "};");
        char *block;
        char *item, *save = NULL;
        long next = 0;
        int plain = 1;
        EnumEntry local[128];
        size_t local_count = 0;
        size_t i;
        if (end == NULL) break;
        block = (char *)malloc((size_t)(end - cursor) + 1);
        if (block == NULL) return;
        memcpy(block, cursor + strlen("enum {"), (size_t)(end - cursor) - strlen("enum {"));
        block[(size_t)(end - cursor) - strlen("enum {")] = '\0';
        strip_comments(block);
        for (item = strtok_r(block, ",", &save); item != NULL && plain;
             item = strtok_r(NULL, ",", &save)) {
            char name[64];
            long value;
            const char *p = item;
            size_t n = 0;
            while (isspace((unsigned char)*p)) ++p;
            if (*p == '\0') continue;
            while (is_identifier_char((unsigned char)*p) && n + 1 < sizeof(name)) name[n++] = *p++;
            name[n] = '\0';
            while (isspace((unsigned char)*p)) ++p;
            if (*p == '=') {
                char *stop;
                ++p;
                value = strtol(p, &stop, 10);
                while (isspace((unsigned char)*stop)) ++stop;
                if (stop == p || *stop != '\0') { plain = 0; break; }
                next = value;
            } else if (*p != '\0') { plain = 0; break; }
            if (local_count < 128) {
                memcpy(local[local_count].name, name, sizeof(name));
                local[local_count].value = next++;
                ++local_count;
            }
        }
        if (plain && local_count > 0 &&
            (strncmp(local[0].name, "AXYNE_CMD_", 10) == 0 ||
             strncmp(local[0].name, "AXYNE_TERMINAL_", 15) == 0)) {
            for (i = 0; i < local_count && entry_count < 256; ++i)
                entries[entry_count++] = local[i];
        }
        free(block);
        cursor = end;
    }
}

static const EnumEntry *find_entry(const char *name)
{
    size_t i;
    for (i = 0; i < entry_count; ++i)
        if (strcmp(entries[i].name, name) == 0) return &entries[i];
    return NULL;
}

static void check_windows_commands(void)
{
    char *source = read_file("src/features/ui/platform/windows/ui_windows.c");
    char *popups, *dispatch, *actions;
    const char *cursor;
    size_t i, j, menu_ids = 0;
    if (source == NULL) return;
    {
        char *copy = strdup(source);
        if (copy != NULL) { strip_comments(copy); parse_enums(copy); free(copy); }
    }
    CHECK(entry_count > 60);
    /* Ids must be unique, and plain command ids must not land in the Open
     * Recent range [AXYNE_CMD_RECENT_BASE, +AXYNE_RECENT_FILES_LIMIT). */
    {
        const EnumEntry *base = find_entry("AXYNE_CMD_RECENT_BASE");
        CHECK(base != NULL);
        for (i = 0; i < entry_count; ++i) {
            for (j = i + 1; j < entry_count; ++j)
                if (entries[i].value == entries[j].value)
                    FAIL("duplicate command id %ld: %s and %s", entries[i].value,
                         entries[i].name, entries[j].name);
            if (base != NULL && strcmp(entries[i].name, "AXYNE_CMD_RECENT_BASE") != 0 &&
                entries[i].value >= base->value &&
                entries[i].value < base->value + AXYNE_RECENT_FILES_LIMIT)
                FAIL("%s collides with the Open Recent id range", entries[i].name);
        }
    }
    popups = region(source, "static void axyne_file_popup(HWND window, AxyneWindowState *state)\n{",
                    "static void axyne_fill(");
    dispatch = region(source, "    case WM_COMMAND: {", "    case WM_CTLCOLOREDIT:");
    actions = region(source, "static int axyne_action_command(HWND window, AxyneWindowState *state, UINT command)\n{",
                     "static int axyne_action_key(");
    if (popups == NULL || dispatch == NULL || actions == NULL) goto done;
    cursor = popups;
    while ((cursor = strstr(cursor, "axyne_menu_add(")) != NULL) {
        char id[64];
        const char *p = strchr(cursor, ',');
        size_t n = 0;
        cursor += strlen("axyne_menu_add(");
        if (p == NULL) break;
        p = strchr(p + 1, ',');          /* skip "menu, &pool," */
        if (p == NULL) break;
        ++p;
        while (isspace((unsigned char)*p)) ++p;
        while (is_identifier_char((unsigned char)*p) && n + 1 < sizeof(id)) id[n++] = *p++;
        id[n] = '\0';
        if (strcmp(id, "AXYNE_CMD_RECENT_BASE") == 0 || strcmp(id, "0") == 0) continue;
        ++menu_ids;
        if (strncmp(id, "AXYNE_CMD_", 10) == 0 || strncmp(id, "AXYNE_TERMINAL_", 15) == 0 ||
            strncmp(id, "AXYNE_DEBUG_", 12) == 0) {
            if (strncmp(id, "AXYNE_DEBUG_", 12) != 0 && find_entry(id) == NULL)
                FAIL("menu id %s is not defined by a parsed enum", id);
            if (!has_word(dispatch, id) && !has_word(actions, id))
                FAIL("Windows menu command %s is never handled", id);
        } else FAIL("unexpected menu id expression: %s", id);
    }
    CHECK(menu_ids >= 60);
    /* Every id handled by the action switch must be inside the id range that
     * axyne_action_command accepts, and every id in that enum needs a case. */
    {
        const EnumEntry *first = find_entry("AXYNE_CMD_GOTO_LINE");
        const EnumEntry *last = find_entry("AXYNE_CMD_REPORT_ISSUE");
        CHECK(first != NULL && last != NULL);
        if (first != NULL && last != NULL) {
            for (i = 0; i < entry_count; ++i) {
                char label[96];
                if (entries[i].value < first->value || entries[i].value > last->value) continue;
                (void)snprintf(label, sizeof(label), "case %s:", entries[i].name);
                if (strstr(actions, label) == NULL)
                    FAIL("%s is in the action id range but has no case", entries[i].name);
            }
        }
    }
done:
    free(popups); free(dispatch); free(actions); free(source);
}

/* ---- both platforms ---------------------------------------------------- */

static int source_has_label(const char *source, char prefix, const char *label)
{
    char needle[160];
    char alternate[160];
    (void)snprintf(needle, sizeof(needle), "%c\"%s\"", prefix, label);
    if (strstr(source, needle) != NULL) return 1;
    (void)snprintf(needle, sizeof(needle), "%c\"%s\xe2\x80\xa6\"", prefix, label);
    (void)snprintf(alternate, sizeof(alternate), "%c\"%s...\"", prefix, label);
    return strstr(source, needle) != NULL || strstr(source, alternate) != NULL;
}

static void check_shared_labels(void)
{
    static const char *const labels[] = {
        "새 파일", "열기", "폴더 열기", "저장", "다른 이름으로 저장", "닫기",
        "실행 취소", "다시 실행", "잘라내기", "복사", "붙여넣기", "모두 선택",
        "찾기", "바꾸기", "파일에서 찾기", "파일 이동", "줄로 이동", "줄 선택",
        "줄 주석 토글", "줄 복제", "줄 위로 이동", "줄 아래로 이동", "들여쓰기",
        "내어쓰기", "정의로 이동", "참조 찾기", "탐색기", "하단 패널", "출력",
        "문제", "터미널", "확대", "축소", "기본 크기", "자동 줄 바꿈", "빌드",
        "실행", "실행 구성", "빌드 취소", "디버깅 시작", "중지", "일시 중지",
        "계속", "프로시저 단위 실행", "한 단계씩 코드 실행", "프로시저 나가기",
        "중단점 토글", "모든 중단점 삭제", "새 터미널", "작업 영역 설정",
        "preferences.json 열기", "Git 상태", "Git 변경 사항", "모두 스테이지",
        "모두 스테이지 해제", "키보드 단축키 참조", "문제 보고", "Axyne 정보"
    };
    char *mac = read_file("src/features/ui/platform/macos/ui_macos.m");
    char *win = read_file("src/features/ui/platform/windows/ui_windows.c");
    size_t i;
    if (mac != NULL && win != NULL) {
        for (i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i) {
            if (!source_has_label(mac, '@', labels[i]))
                FAIL("macOS menu lacks shared entry: %s", labels[i]);
            if (!source_has_label(win, 'L', labels[i]))
                FAIL("Windows menu lacks shared entry: %s", labels[i]);
        }
    }
    free(mac); free(win);
}

int main(void)
{
    check_macos_selectors();
    check_windows_commands();
    check_shared_labels();
    if (failures == 0) puts("menu consistency ok");
    return failures == 0 ? 0 : 1;
}
