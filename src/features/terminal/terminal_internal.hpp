#pragma once
#include "axyne/terminal.h"
#include <vterm.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct AxyneTerminal {
    std::mutex model_mutex, queue_mutex;
    std::condition_variable queue_ready;
    VTerm *vt = nullptr;
    VTermScreen *screen = nullptr;
    std::deque<std::vector<VTermScreenCell>> history;
    size_t history_limit = 0, queued_bytes = 0;
    int scroll = 0, cursor_visible = 1, alternate = 0;
    std::deque<std::string> writes;
    std::string encoded;
    bool encoding = false;
    std::atomic<bool> running{false}, stopping{false};
    std::thread reader, writer;
    AxyneTerminalChangedFn changed = nullptr;
    AxyneTerminalExitFn exited = nullptr;
    void *context = nullptr;
    void *transport = nullptr;
};
AxyneStatus terminal_result(AxyneError *, AxyneStatus, const char * = "");
AxyneStatus terminal_enqueue(AxyneTerminal *, const char *, size_t, AxyneError *);
AxyneStatus terminal_transport_start(AxyneTerminal *, const AxyneTerminalSpec *, AxyneError *);
AxyneStatus terminal_transport_resize(AxyneTerminal *, int, int, AxyneError *);
void terminal_transport_stop(AxyneTerminal *);
void terminal_transport_destroy(AxyneTerminal *);
