#ifndef AXYNE_EDITOR_ACTIONS_H
#define AXYNE_EDITOR_ACTIONS_H

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "Scintilla.h"

/* Thin, platform-independent editor commands built on Scintilla messages.
 * Both native adapters pass their own message sender so the behaviour (and
 * its tests) are shared; nothing here touches UI. */

#ifndef AXYNE_EDITOR_MESSAGE_DEFINED
#define AXYNE_EDITOR_MESSAGE_DEFINED
typedef intptr_t (*AxyneEditorMessage)(void *editor, unsigned int message,
                                      uintptr_t w_param, intptr_t l_param);
#endif

/* Returns the single-line comment token for the file, or NULL when the
 * language has no line comment (or is not recognised), in which case the
 * Toggle Line Comment command is disabled rather than guessing. Untitled
 * buffers use the C-family lexer, so they get "//". */
static inline const char *axyne_editor_comment_token(const char *path)
{
    const char *name = path;
    const char *slash;
    const char *backslash;
    const char *dot;
    char extension[16];
    size_t length;
    size_t i;
    static const char *const slashes[] = {
        "c", "h", "cc", "cpp", "cxx", "hpp", "hh", "m", "mm", "js", "jsx",
        "mjs", "ts", "tsx", "java", "cs", "go", "rs", "swift", "kt", "kts", "slint", NULL };
    static const char *const hashes[] = {
        "py", "sh", "bash", "zsh", "rb", "pl", "ps1", "toml", "yml", "yaml",
        "cmake", "mk", NULL };
    if (path == NULL || path[0] == '\0') return "//";
    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
    if (slash != NULL) name = slash + 1;
    if (strcmp(name, "CMakeLists.txt") == 0 || strcmp(name, "Makefile") == 0 ||
        strcmp(name, "Dockerfile") == 0) return "#";
    dot = strrchr(name, '.');
    if (dot == NULL || dot == name || dot[1] == '\0') return NULL;
    length = strlen(dot + 1);
    if (length >= sizeof(extension)) return NULL;
    for (i = 0; i < length; ++i)
        extension[i] = (char)tolower((unsigned char)dot[1 + i]);
    extension[length] = '\0';
    for (i = 0; slashes[i] != NULL; ++i)
        if (strcmp(extension, slashes[i]) == 0) return "//";
    for (i = 0; hashes[i] != NULL; ++i)
        if (strcmp(extension, hashes[i]) == 0) return "#";
    return NULL;
}

/* Parses a 1-based line number typed by the user. Rejects empty text, signs
 * and trailing characters, and numbers outside 1..line_count. */
static inline int axyne_editor_parse_line_number(const char *text,
                                                 size_t line_count,
                                                 size_t *line)
{
    size_t value = 0;
    if (text == NULL || line == NULL || line_count == 0) return 0;
    while (*text == ' ' || *text == '\t') ++text;
    if (*text < '0' || *text > '9') return 0;
    while (*text >= '0' && *text <= '9') {
        size_t digit = (size_t)(*text - '0');
        if (value > (SIZE_MAX - digit) / 10) return 0;
        value = value * 10 + digit;
        ++text;
    }
    while (*text == ' ' || *text == '\t') ++text;
    if (*text != '\0' || value < 1 || value > line_count) return 0;
    *line = value;
    return 1;
}

static inline int axyne_editor_go_to_line(AxyneEditorMessage send, void *editor,
                                          size_t line)
{
    intptr_t count = send(editor, SCI_GETLINECOUNT, 0, 0);
    if (line < 1 || (intptr_t)line > count) return 0;
    send(editor, SCI_GOTOLINE, (uintptr_t)(line - 1), 0);
    return 1;
}

static inline int axyne_editor_select_line(AxyneEditorMessage send, void *editor)
{
    intptr_t position = send(editor, SCI_GETCURRENTPOS, 0, 0);
    intptr_t line = send(editor, SCI_LINEFROMPOSITION, (uintptr_t)position, 0);
    intptr_t count = send(editor, SCI_GETLINECOUNT, 0, 0);
    intptr_t start = send(editor, SCI_POSITIONFROMLINE, (uintptr_t)line, 0);
    intptr_t end = line + 1 < count
        ? send(editor, SCI_POSITIONFROMLINE, (uintptr_t)(line + 1), 0)
        : send(editor, SCI_GETLENGTH, 0, 0);
    send(editor, SCI_SETSEL, (uintptr_t)start, end);
    return 1;
}

static inline int axyne_editor_token_at(AxyneEditorMessage send, void *editor,
                                        intptr_t position, const char *token)
{
    size_t i;
    for (i = 0; token[i] != '\0'; ++i)
        if ((char)send(editor, SCI_GETCHARAT, (uintptr_t)(position + (intptr_t)i), 0)
                != token[i]) return 0;
    return 1;
}

/* Comments every non-blank line touched by the selection, or removes the
 * token from all of them when each already starts with it. One undo step.
 * Returns 0 when nothing changed (read-only buffer, only blank lines). */
static inline int axyne_editor_toggle_line_comment(AxyneEditorMessage send,
                                                   void *editor,
                                                   const char *token)
{
    size_t token_length;
    intptr_t selection_start, selection_end, first, last, line;
    int has_content = 0;
    int all_commented = 1;
    int changed = 0;
    if (token == NULL || token[0] == '\0') return 0;
    token_length = strlen(token);
    if (token_length > 8) return 0;
    if (send(editor, SCI_GETREADONLY, 0, 0) != 0) return 0;
    selection_start = send(editor, SCI_GETSELECTIONSTART, 0, 0);
    selection_end = send(editor, SCI_GETSELECTIONEND, 0, 0);
    first = send(editor, SCI_LINEFROMPOSITION, (uintptr_t)selection_start, 0);
    last = send(editor, SCI_LINEFROMPOSITION, (uintptr_t)selection_end, 0);
    /* A selection ending at a line start does not include that line. */
    if (selection_end > selection_start && last > first &&
        send(editor, SCI_POSITIONFROMLINE, (uintptr_t)last, 0) == selection_end)
        --last;
    for (line = first; line <= last; ++line) {
        intptr_t indent = send(editor, SCI_GETLINEINDENTPOSITION, (uintptr_t)line, 0);
        char first_char = (char)send(editor, SCI_GETCHARAT, (uintptr_t)indent, 0);
        if (first_char == '\0' || first_char == '\r' || first_char == '\n') continue;
        has_content = 1;
        if (!axyne_editor_token_at(send, editor, indent, token)) all_commented = 0;
    }
    if (!has_content) return 0;
    send(editor, SCI_BEGINUNDOACTION, 0, 0);
    for (line = last; line >= first; --line) {
        intptr_t indent = send(editor, SCI_GETLINEINDENTPOSITION, (uintptr_t)line, 0);
        char first_char = (char)send(editor, SCI_GETCHARAT, (uintptr_t)indent, 0);
        if (first_char == '\0' || first_char == '\r' || first_char == '\n') continue;
        if (all_commented) {
            size_t remove = token_length;
            if ((char)send(editor, SCI_GETCHARAT,
                    (uintptr_t)(indent + (intptr_t)token_length), 0) == ' ') ++remove;
            send(editor, SCI_DELETERANGE, (uintptr_t)indent, (intptr_t)remove);
        } else {
            char text[16];
            memcpy(text, token, token_length);
            text[token_length] = ' ';
            text[token_length + 1] = '\0';
            send(editor, SCI_INSERTTEXT, (uintptr_t)indent, (intptr_t)text);
        }
        changed = 1;
    }
    send(editor, SCI_ENDUNDOACTION, 0, 0);
    if (changed && selection_end > selection_start) {
        intptr_t start = send(editor, SCI_POSITIONFROMLINE, (uintptr_t)first, 0);
        intptr_t end = send(editor, SCI_GETLINEENDPOSITION, (uintptr_t)last, 0);
        send(editor, SCI_SETSEL, (uintptr_t)start, end);
    }
    return changed;
}

#endif
