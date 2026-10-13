#include "axyne/terminal.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
using Clock = std::chrono::steady_clock;
static std::string text(AxyneTerminal *t) {
    AxyneTerminalSnapshot s{}; assert(axyne_terminal_snapshot(t, &s, nullptr) == AXYNE_STATUS_OK);
    std::string result;
    for (int r = 0; r < s.rows; ++r) {
        for (int c = 0; c < s.columns; ++c) { uint32_t ch = s.cells[r*s.columns+c].chars[0]; result += ch && ch < 128 ? char(ch) : ' '; }
        result += '\n';
    }
    axyne_terminal_snapshot_free(&s); return result;
}
static void await_text(AxyneTerminal *t, const char *expected) {
    auto deadline = Clock::now() + std::chrono::seconds(5);
    while (text(t).find(expected) == std::string::npos && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (text(t).find(expected) == std::string::npos) { std::cerr << text(t); std::abort(); }
}
static void send(AxyneTerminal *t, const char *s) { assert(axyne_terminal_write(t, s, std::strlen(s), nullptr) == AXYNE_STATUS_OK); }
int main() {
    AxyneTerminal *t; assert(axyne_terminal_create(24, 100, 2000, &t, nullptr) == AXYNE_STATUS_OK);
#ifdef _WIN32
    const char *args[] = {"/Q"}; AxyneTerminalSpec spec{"C:\\Windows\\System32\\cmd.exe", args, 1, nullptr, nullptr, nullptr, nullptr};
#else
    const char *args[] = {"-f", "-i"}; AxyneTerminalSpec spec{"/bin/zsh", args, 2, "/tmp", nullptr, nullptr, nullptr};
#endif
    AxyneError e{}; assert(axyne_terminal_start(t, &spec, &e) == AXYNE_STATUS_OK);
#ifdef _WIN32
    send(t, "echo AXYNE_READY\r"); await_text(t, "AXYNE_READY");
#else
    send(t, "unsetopt zle; PS1='AXYNE_PROMPT> '; setopt zle\r"); await_text(t, "AXYNE_PROMPT>");
    send(t, "[[ -t 0 && -t 1 && -t 2 ]] && print TTY_OK; print CWD:$PWD\r"); await_text(t, "TTY_OK"); await_text(t, "CWD:/");
    send(t, "print HISTORY_OK\r"); await_text(t, "HISTORY_OK");
    assert(axyne_terminal_key(t, AXYNE_TERMINAL_UP, 0, nullptr) == AXYNE_STATUS_OK);
    assert(axyne_terminal_key(t, AXYNE_TERMINAL_ENTER, 0, nullptr) == AXYNE_STATUS_OK);
    send(t, "sleep 20\r"); std::this_thread::sleep_for(std::chrono::milliseconds(150));
    assert(axyne_terminal_unichar(t, 'c', AXYNE_TERMINAL_CTRL, nullptr) == AXYNE_STATUS_OK);
    send(t, "print INTERRUPT_OK\r"); await_text(t, "INTERRUPT_OK"); assert(axyne_terminal_running(t));
#endif
    assert(axyne_terminal_resize(t, 30, 120, nullptr) == AXYNE_STATUS_OK);
    send(t, "echo FINAL_DRAIN; exit\r");
    auto deadline = Clock::now() + std::chrono::seconds(5);
    while (axyne_terminal_running(t) && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(!axyne_terminal_running(t)); await_text(t, "FINAL_DRAIN"); axyne_terminal_release(t);
    for (int i = 0; i < 8; ++i) {
        assert(axyne_terminal_create(24, 80, 20, &t, nullptr) == AXYNE_STATUS_OK);
        assert(axyne_terminal_start(t, &spec, &e) == AXYNE_STATUS_OK);
        auto start = Clock::now(); axyne_terminal_release(t);
        assert(Clock::now() - start < std::chrono::seconds(3));
    }
    assert(axyne_terminal_create(24, 80, 20, &t, nullptr) == AXYNE_STATUS_OK);
    spec.executable = "/axyne/missing/shell";
    assert(axyne_terminal_start(t, &spec, &e) != AXYNE_STATUS_OK); assert(!axyne_terminal_running(t)); axyne_terminal_release(t);
    std::cout << "Native session tests passed\n";
}
