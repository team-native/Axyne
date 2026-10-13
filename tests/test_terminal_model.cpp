#include "../src/features/terminal/terminal_internal.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
static void feed(AxyneTerminal *t, const char *s) { axyne_terminal_feed(t, s, std::strlen(s)); }
static AxyneTerminalSnapshot snapshot(AxyneTerminal *t) {
    AxyneTerminalSnapshot s{}; assert(axyne_terminal_snapshot(t, &s, nullptr) == AXYNE_STATUS_OK); return s;
}
int main() {
    AxyneTerminal *t = nullptr;
    assert(axyne_terminal_create(4, 20, 8, &t, nullptr) == AXYNE_STATUS_OK);
    feed(t, "abc\rX\033[2;4HY\033[38;2;12;34;56m\033[48;5;196m\033[1;4mZ");
    auto s = snapshot(t);
    assert(s.cells[0].chars[0] == 'X' && s.cells[1].chars[0] == 'b');
    assert(s.cells[23].chars[0] == 'Y' && s.cells[24].chars[0] == 'Z');
    assert(s.cells[24].foreground == 0x0c2238 && s.cells[24].background == 0xff0000);
    assert((s.cells[24].attributes & 5) == 5); axyne_terminal_snapshot_free(&s);
    feed(t, "\033[0m\033[2J\033[H");
    axyne_terminal_feed(t, "\xe4", 1); axyne_terminal_feed(t, "\xb8\xad", 2);
    s = snapshot(t); assert(s.cells[0].chars[0] == 0x4e2d && s.cells[0].width == 2 && s.cells[1].width == 0);
    axyne_terminal_snapshot_free(&s);
    feed(t, "\033[?1049hALT"); s = snapshot(t); assert(s.alternate_screen); axyne_terminal_snapshot_free(&s);
    feed(t, "\033[?1049l"); s = snapshot(t); assert(!s.alternate_screen && s.cells[0].chars[0] == 0x4e2d); axyne_terminal_snapshot_free(&s);
    feed(t, "\033[?1h"); axyne_terminal_key(t, AXYNE_TERMINAL_UP, 0, nullptr); assert(t->encoded == "\033OA");
    feed(t, "\033[?1l"); axyne_terminal_key(t, AXYNE_TERMINAL_UP, 0, nullptr); assert(t->encoded == "\033[A");
    feed(t, "\033[?2004h"); axyne_terminal_paste(t, "hello", 5, nullptr); assert(t->encoded == "\033[200~hello\033[201~");
    axyne_terminal_unichar(t, 'c', AXYNE_TERMINAL_CTRL, nullptr); assert(t->encoded == "\003");
    axyne_terminal_clear(t); feed(t, "one\r\ntwo\r\nthree\r\nfour\r\nfive\r\n");
    s = snapshot(t); assert(s.history_lines == 2); axyne_terminal_snapshot_free(&s);
    axyne_terminal_scroll(t, 2); s = snapshot(t); assert(s.cells[0].chars[0] == 'o' && !s.cursor_visible); axyne_terminal_snapshot_free(&s);
    assert(axyne_terminal_resize(t, 6, 30, nullptr) == AXYNE_STATUS_OK);
    s = snapshot(t); assert(s.rows == 6 && s.columns == 30); axyne_terminal_snapshot_free(&s);
    assert(axyne_terminal_resize(t, 513, 20, nullptr) == AXYNE_STATUS_INVALID_ARGUMENT);
    axyne_terminal_set_colors(t, 0xabcdef, 0x123456); axyne_terminal_clear(t);
    s = snapshot(t); assert(s.cells[0].foreground == 0xabcdef && s.cells[0].background == 0x123456); axyne_terminal_snapshot_free(&s);
    axyne_terminal_release(t); std::cout << "VT model tests passed\n";
}
