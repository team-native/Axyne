#include "axyne/terminal.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
struct Events { std::atomic<int> changed{0}, exits{0}, code{-1}; };
static void changed(AxyneTerminal *, void *p) { auto *e = static_cast<Events *>(p); assert(e->exits == 0); ++e->changed; }
static void exited(AxyneTerminal *, int code, void *p) { auto *e = static_cast<Events *>(p); e->code = code; ++e->exits; }
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
    if (text(t).find(expected) == std::string::npos) {
        std::cerr << "Terminal wait failed: marker=" << expected << " running=" << axyne_terminal_running(t) << "\n" << text(t) << std::flush;
        axyne_terminal_release(t); std::exit(EXIT_FAILURE);
    }
}
static void send(AxyneTerminal *t, const char *s) { assert(axyne_terminal_write(t, s, std::strlen(s), nullptr) == AXYNE_STATUS_OK); }
#ifdef _WIN32
static void command(AxyneTerminal *t, const char *s) {
    AxyneError error{};
    for (;*s;++s) {
        auto status=axyne_terminal_unichar(t,static_cast<unsigned char>(*s),0,&error);
        if(status!=AXYNE_STATUS_OK) {std::cerr<<"ConPTY typing failed: "<<error.message<<"\n";axyne_terminal_release(t);std::exit(EXIT_FAILURE);}
    }
    assert(axyne_terminal_key(t,AXYNE_TERMINAL_ENTER,0,&error)==AXYNE_STATUS_OK);
}
#endif
int main() {
    Events events;
    AxyneTerminal *t; assert(axyne_terminal_create(24, 100, 2000, &t, nullptr) == AXYNE_STATUS_OK);
#ifdef _WIN32
    const char *args[] = {"/Q"}; AxyneTerminalSpec spec{"C:\\Windows\\System32\\cmd.exe", args, 1, nullptr, nullptr, nullptr, nullptr};
#else
    const char *args[] = {"-f", "-i"}; AxyneTerminalSpec spec{"/bin/zsh", args, 2, "/tmp", nullptr, nullptr, nullptr};
#endif
    spec.on_changed = changed; spec.on_exit = exited; spec.user_data = &events;
    AxyneError e{}; assert(axyne_terminal_start(t, &spec, &e) == AXYNE_STATUS_OK);
#ifdef _WIN32
    // A console application can flush early input during startup. Establish
    // its prompt before typing; exercise libvterm characters and Enter.
    await_text(t, ">");
    command(t, "set AXYNE_PREFIX=AXYNE_");
    command(t, "echo %AXYNE_PREFIX%READY"); await_text(t, "AXYNE_READY");
#else
    send(t, "unsetopt zle; PS1='AXYNE_PROMPT> '; setopt zle\r"); await_text(t, "AXYNE_PROMPT>");
    send(t, "[[ -t 0 && -t 1 && -t 2 ]] && printf '%s%s\\n' TTY_ OK; print CWD:$PWD\r"); await_text(t, "TTY_OK"); await_text(t, "CWD:/");
    send(t, "printf '%s%s\\n' HISTORY_ OK\r"); await_text(t, "HISTORY_OK");
    assert(axyne_terminal_key(t, AXYNE_TERMINAL_UP, 0, nullptr) == AXYNE_STATUS_OK);
    assert(axyne_terminal_key(t, AXYNE_TERMINAL_ENTER, 0, nullptr) == AXYNE_STATUS_OK);
    auto history_deadline = Clock::now() + std::chrono::seconds(5);
    while (Clock::now() < history_deadline) { auto value = text(t); auto first = value.find("HISTORY_OK"); if (first != std::string::npos && value.find("HISTORY_OK", first+1) != std::string::npos) break; std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    { auto value = text(t); auto first = value.find("HISTORY_OK"); assert(first != std::string::npos && value.find("HISTORY_OK", first+1) != std::string::npos); }
    send(t, "sleep 20\r"); std::this_thread::sleep_for(std::chrono::milliseconds(150));
    assert(axyne_terminal_unichar(t, 'c', AXYNE_TERMINAL_CTRL, nullptr) == AXYNE_STATUS_OK);
    send(t, "printf '%s%s\\n' INTERRUPT_ OK\r"); await_text(t, "INTERRUPT_OK"); assert(axyne_terminal_running(t));
#endif
    assert(axyne_terminal_resize(t, 30, 120, nullptr) == AXYNE_STATUS_OK);
#ifdef _WIN32
    command(t, "set AXYNE_A=FINAL_"); command(t, "echo %AXYNE_A%DRAIN"); command(t,"exit 7");
#else
    send(t, "printf '%s%s\\n' FINAL_ DRAIN; exit 7\r");
#endif
    auto deadline = Clock::now() + std::chrono::seconds(5);
    while (axyne_terminal_running(t) && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(!axyne_terminal_running(t)); await_text(t, "FINAL_DRAIN"); axyne_terminal_release(t);
    assert(events.exits == 1 && events.code == 7 && events.changed > 0);
    spec.on_changed = nullptr; spec.on_exit = nullptr; spec.user_data = nullptr;
#ifndef _WIN32
    assert(axyne_terminal_create(24, 80, 20, &t, nullptr) == AXYNE_STATUS_OK);
    const char *eofArgs[] = {"-f", "-c", "exec </dev/null >/dev/null 2>&1; sleep 1; exit 7"};
    Events eofEvents;
    AxyneTerminalSpec eofSpec{"/bin/zsh",eofArgs,3,"/tmp",changed,exited,&eofEvents};
    assert(axyne_terminal_start(t,&eofSpec,&e)==AXYNE_STATUS_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(1300));
    axyne_terminal_release(t); assert(eofEvents.exits==1&&eofEvents.code==7);
#endif
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
