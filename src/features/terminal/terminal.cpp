#include "terminal_internal.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <new>

static constexpr size_t input_limit = 1024 * 1024;
static bool dimensions(int rows, int cols) {
    return rows >= 1 && rows <= 512 && cols >= 1 && cols <= 1024;
}
AxyneStatus terminal_result(AxyneError *e, AxyneStatus s, const char *message) {
    if (e) { e->code = s; std::snprintf(e->message, sizeof e->message, "%s", message); }
    return s;
}
AxyneStatus terminal_enqueue(AxyneTerminal *t, const char *data, size_t len, AxyneError *e) {
    std::lock_guard<std::mutex> lock(t->queue_mutex);
    if (!t->running || t->stopping)
        return terminal_result(e, AXYNE_STATUS_NOT_FOUND, "Terminal session is not running");
    if (len > input_limit - t->queued_bytes)
        return terminal_result(e, AXYNE_STATUS_BUSY, "Terminal input queue is full (1 MiB)");
    try { if (len) t->writes.emplace_back(data, len); }
    catch (const std::bad_alloc &) { return terminal_result(e, AXYNE_STATUS_OUT_OF_MEMORY); }
    t->queued_bytes += len;
    t->queue_ready.notify_one();
    return terminal_result(e, AXYNE_STATUS_OK);
}
static void output(const char *s, size_t n, void *context) {
    auto *t = static_cast<AxyneTerminal *>(context);
    if (t->encoding) {
        try { t->encoded.append(s, n); }
        catch (...) { t->encoding_failed = true; }
    }
    else if (t->running) terminal_enqueue(t, s, n, nullptr);
}
static int property(VTermProp prop, VTermValue *v, void *context) {
    auto *t = static_cast<AxyneTerminal *>(context);
    if (prop == VTERM_PROP_CURSORVISIBLE) t->cursor_visible = v->boolean;
    if (prop == VTERM_PROP_ALTSCREEN) { t->alternate = v->boolean; t->scroll = 0; }
    return 1;
}
static int pushline(int cols, const VTermScreenCell *cells, void *context) {
    auto *t = static_cast<AxyneTerminal *>(context);
    if (!t->history_limit || t->alternate) return 1;
    try { t->history.emplace_back(cells, cells + cols); }
    catch (const std::bad_alloc &) { return 0; }
    if (t->scroll) ++t->scroll;
    if (t->history.size() > t->history_limit) t->history.pop_front();
    t->scroll = std::min(t->scroll, static_cast<int>(t->history.size()));
    return 1;
}
static int popline(int cols, VTermScreenCell *cells, void *context) {
    auto *t = static_cast<AxyneTerminal *>(context);
    if (t->history.empty() || t->alternate) return 0;
    const auto &line = t->history.back();
    VTermColor fg, bg;
    vterm_state_get_default_colors(vterm_obtain_state(t->vt), &fg, &bg);
    for (int c = 0; c < cols; ++c) {
        if (c < static_cast<int>(line.size())) cells[c] = line[c];
        else { cells[c] = {}; cells[c].width = 1; cells[c].fg = fg; cells[c].bg = bg; }
    }
    t->history.pop_back();
    t->scroll = std::min(t->scroll, static_cast<int>(t->history.size()));
    return 1;
}
static int clear_history(void *context) {
    auto *t = static_cast<AxyneTerminal *>(context);
    t->history.clear(); t->scroll = 0; return 1;
}
static const VTermScreenCallbacks callbacks = {
    nullptr, nullptr, nullptr, property, nullptr, nullptr, pushline, popline, clear_history
};
extern "C" AxyneStatus axyne_terminal_create(int rows, int cols, size_t limit,
                                              AxyneTerminal **out, AxyneError *e) try {
    if (!out || !dimensions(rows, cols) || limit > 2000)
        return terminal_result(e, AXYNE_STATUS_INVALID_ARGUMENT, "Terminal dimensions exceed 512 rows / 1024 columns or history exceeds 2000 lines");
    *out = nullptr;
    auto *t = new (std::nothrow) AxyneTerminal;
    if (!t) return terminal_result(e, AXYNE_STATUS_OUT_OF_MEMORY);
    t->vt = vterm_new(rows, cols);
    if (!t->vt) { delete t; return terminal_result(e, AXYNE_STATUS_OUT_OF_MEMORY); }
    t->history_limit = limit;
    vterm_set_utf8(t->vt, 1);
    t->screen = vterm_obtain_screen(t->vt);
    vterm_screen_enable_altscreen(t->screen, 1);
    vterm_screen_set_callbacks(t->screen, &callbacks, t);
    vterm_output_set_callback(t->vt, output, t);
    vterm_screen_reset(t->screen, 1);
    *out = t;
    return terminal_result(e, AXYNE_STATUS_OK);
} catch (...) {
    if (out) *out = nullptr;
    return terminal_result(e, AXYNE_STATUS_OUT_OF_MEMORY, "Cannot allocate terminal model");
}
extern "C" void axyne_terminal_feed(AxyneTerminal *t, const char *s, size_t n) try {
    if (!t || (!s && n)) return;
    std::lock_guard<std::mutex> lock(t->model_mutex);
    vterm_input_write(t->vt, s, n); vterm_screen_flush_damage(t->screen);
}
catch (...) {}
extern "C" AxyneStatus axyne_terminal_write(AxyneTerminal *t, const char *s, size_t n, AxyneError *e) try {
    if (!t || (!s && n)) return terminal_result(e, AXYNE_STATUS_INVALID_ARGUMENT);
    return terminal_enqueue(t, s, n, e);
}
catch (...) { return terminal_result(e, AXYNE_STATUS_IO_ERROR, "Terminal operation failed"); }
template<class F> static AxyneStatus encode(AxyneTerminal *t, F f, AxyneError *e) {
    if (!t) return terminal_result(e, AXYNE_STATUS_INVALID_ARGUMENT);
    std::lock_guard<std::mutex> lock(t->model_mutex);
    t->encoding = true; t->encoding_failed = false; t->encoded.clear();
    try { f(); }
    catch (const std::bad_alloc &) { t->encoding = false; return terminal_result(e, AXYNE_STATUS_OUT_OF_MEMORY); }
    t->encoding = false; t->scroll = 0;
    if (t->encoding_failed) return terminal_result(e, AXYNE_STATUS_OUT_OF_MEMORY);
    return terminal_enqueue(t, t->encoded.data(), t->encoded.size(), e);
}
extern "C" AxyneStatus axyne_terminal_key(AxyneTerminal *t, AxyneTerminalKey k, unsigned mods, AxyneError *e) try {
    if ((k < 1 || k > AXYNE_TERMINAL_PAGEDOWN) && (k < 257 || k > 280))
        return terminal_result(e, AXYNE_STATUS_INVALID_ARGUMENT);
    return encode(t, [&]{ vterm_keyboard_key(t->vt, static_cast<VTermKey>(k), static_cast<VTermModifier>(mods & 7)); }, e);
}
catch (...) { return terminal_result(e, AXYNE_STATUS_IO_ERROR, "Terminal operation failed"); }
extern "C" AxyneStatus axyne_terminal_unichar(AxyneTerminal *t, uint32_t c, unsigned mods, AxyneError *e) try {
    if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return terminal_result(e, AXYNE_STATUS_INVALID_ARGUMENT);
    return encode(t, [&]{ vterm_keyboard_unichar(t->vt, c, static_cast<VTermModifier>(mods & 7)); }, e);
}
catch (...) { return terminal_result(e, AXYNE_STATUS_IO_ERROR, "Terminal operation failed"); }
extern "C" AxyneStatus axyne_terminal_paste(AxyneTerminal *t, const char *s, size_t n, AxyneError *e) try {
    if ((!s && n) || n > input_limit - 12) return terminal_result(e, AXYNE_STATUS_INVALID_ARGUMENT);
    return encode(t, [&]{ vterm_keyboard_start_paste(t->vt); t->encoded.append(s ? s : "", n); vterm_keyboard_end_paste(t->vt); }, e);
}
catch (...) { return terminal_result(e, AXYNE_STATUS_IO_ERROR, "Terminal operation failed"); }
extern "C" AxyneStatus axyne_terminal_resize(AxyneTerminal *t, int rows, int cols, AxyneError *e) try {
    if (!t || !dimensions(rows, cols)) return terminal_result(e, AXYNE_STATUS_INVALID_ARGUMENT);
    { std::lock_guard<std::mutex> lock(t->model_mutex);
      vterm_set_size(t->vt, rows, cols); vterm_screen_flush_damage(t->screen); }
    return t->running ? terminal_transport_resize(t, rows, cols, e) : terminal_result(e, AXYNE_STATUS_OK);
}
catch (...) { return terminal_result(e, AXYNE_STATUS_IO_ERROR, "Terminal operation failed"); }
extern "C" void axyne_terminal_scroll(AxyneTerminal *t, int lines) try {
    if (!t) return;
    std::lock_guard<std::mutex> lock(t->model_mutex);
    t->scroll = t->alternate ? 0 : static_cast<int>(std::clamp(static_cast<long long>(t->scroll) + lines, 0LL, static_cast<long long>(t->history.size())));
}
catch (...) {}
extern "C" void axyne_terminal_clear(AxyneTerminal *t) try {
    if (!t) return;
    std::lock_guard<std::mutex> lock(t->model_mutex);
    t->history.clear(); t->scroll = 0; vterm_screen_reset(t->screen, 1);
}
catch (...) {}
extern "C" void axyne_terminal_set_colors(AxyneTerminal *t, uint32_t fg, uint32_t bg) try {
    if (!t) return;
    std::lock_guard<std::mutex> lock(t->model_mutex);
    VTermColor f, b; vterm_color_rgb(&f, fg >> 16, fg >> 8, fg); vterm_color_rgb(&b, bg >> 16, bg >> 8, bg);
    vterm_screen_set_default_colors(t->screen, &f, &b);
}
catch (...) {}
static uint32_t rgb(AxyneTerminal *t, VTermColor color) {
    vterm_screen_convert_color_to_rgb(t->screen, &color);
    return (color.rgb.red << 16) | (color.rgb.green << 8) | color.rgb.blue;
}
extern "C" AxyneStatus axyne_terminal_snapshot(AxyneTerminal *t, AxyneTerminalSnapshot *s, AxyneError *e) try {
    if (!t || !s) return terminal_result(e, AXYNE_STATUS_INVALID_ARGUMENT);
    std::lock_guard<std::mutex> lock(t->model_mutex);
    *s = {}; vterm_get_size(t->vt, &s->rows, &s->columns);
    s->cells = static_cast<AxyneTerminalCell *>(std::calloc(static_cast<size_t>(s->rows) * s->columns, sizeof *s->cells));
    if (!s->cells) return terminal_result(e, AXYNE_STATUS_OUT_OF_MEMORY);
    VTermPos pos; vterm_state_get_cursorpos(vterm_obtain_state(t->vt), &pos);
    s->cursor_row = pos.row; s->cursor_column = pos.col; s->cursor_visible = t->cursor_visible && !t->scroll;
    s->alternate_screen = t->alternate; s->scroll_offset = t->scroll; s->history_lines = t->history.size();
    VTermColor fg, bg; vterm_state_get_default_colors(vterm_obtain_state(t->vt), &fg, &bg);
    for (int r = 0; r < s->rows; ++r) for (int c = 0; c < s->columns; ++c) {
        VTermScreenCell cell{}; cell.width = 1; cell.fg = fg; cell.bg = bg;
        int line = static_cast<int>(t->history.size()) - t->scroll + r;
        if (line < static_cast<int>(t->history.size())) {
            const auto &h = t->history[line]; if (c < static_cast<int>(h.size())) cell = h[c];
        } else vterm_screen_get_cell(t->screen, {r - t->scroll, c}, &cell);
        auto &dest = s->cells[r * s->columns + c];
        std::memcpy(dest.chars, cell.chars, sizeof dest.chars);
        dest.width = cell.chars[0] == UINT32_MAX ? 0 : cell.width;
        if (!dest.width) std::memset(dest.chars, 0, sizeof dest.chars);
        dest.foreground = rgb(t, cell.fg); dest.background = rgb(t, cell.bg);
        dest.attributes = (cell.attrs.bold ? AXYNE_TERMINAL_BOLD : 0) | (cell.attrs.italic ? AXYNE_TERMINAL_ITALIC : 0) |
            (cell.attrs.underline ? AXYNE_TERMINAL_UNDERLINE : 0) | (cell.attrs.reverse ? AXYNE_TERMINAL_REVERSE : 0);
    }
    return terminal_result(e, AXYNE_STATUS_OK);
}
catch (...) { return terminal_result(e, AXYNE_STATUS_IO_ERROR, "Terminal operation failed"); }
extern "C" void axyne_terminal_snapshot_free(AxyneTerminalSnapshot *s) try { if (s) { std::free(s->cells); *s = {}; } }
catch (...) {}
extern "C" int axyne_terminal_running(AxyneTerminal *t) try { return t && t->running; }
catch (...) { return 0; }
extern "C" AxyneStatus axyne_terminal_start(AxyneTerminal *t, const AxyneTerminalSpec *spec, AxyneError *e) try {
    if (!t || !spec || !spec->executable || (spec->argument_count && !spec->arguments)) return terminal_result(e, AXYNE_STATUS_INVALID_ARGUMENT);
    if (t->running) return terminal_result(e, AXYNE_STATUS_BUSY);
    terminal_transport_destroy(t);
    t->changed = spec->on_changed; t->exited = spec->on_exit; t->context = spec->user_data;
    t->stopping = false;
    { std::lock_guard<std::mutex> lock(t->queue_mutex); t->writes.clear(); t->queued_bytes = 0; }
    return terminal_transport_start(t, spec, e);
} catch (const std::bad_alloc &) {
    if (t) terminal_transport_destroy(t);
    return terminal_result(e, AXYNE_STATUS_OUT_OF_MEMORY, "Cannot allocate terminal session");
} catch (...) {
    if (t) terminal_transport_destroy(t);
    return terminal_result(e, AXYNE_STATUS_IO_ERROR, "Cannot initialize terminal session");
}
extern "C" AxyneStatus axyne_terminal_terminate(AxyneTerminal *t, AxyneError *e) try {
    if (!t) return terminal_result(e, AXYNE_STATUS_INVALID_ARGUMENT);
    terminal_transport_stop(t); return terminal_result(e, AXYNE_STATUS_OK);
}
catch (...) { return terminal_result(e, AXYNE_STATUS_IO_ERROR, "Terminal operation failed"); }
extern "C" void axyne_terminal_release(AxyneTerminal *t) try {
    if (!t) return;
    terminal_transport_destroy(t); vterm_free(t->vt); delete t;
}
catch (...) {}
