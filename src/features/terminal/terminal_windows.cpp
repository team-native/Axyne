#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "terminal_internal.hpp"
#include <chrono>
#include <memory>

using CreatePC = HRESULT (WINAPI *)(COORD, HANDLE, HANDLE, DWORD, HANDLE *);
using ResizePC = HRESULT (WINAPI *)(HANDLE, COORD);
using ClosePC = void (WINAPI *)(HANDLE);
struct WindowsTerminal {
    HANDLE input = nullptr, output = nullptr, process = nullptr, job = nullptr, pc = nullptr;
    ResizePC resize = nullptr; ClosePC close = nullptr;
    std::mutex pc_mutex;
    std::thread closer;
    std::atomic<bool> closed{false};
    DWORD exit_code = 0;
};
static AxyneStatus failure(AxyneError *e, const char *message) { return terminal_result(e, AXYNE_STATUS_IO_ERROR, message); }
static std::wstring wide(const char *s) {
    if (!s) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, nullptr, 0);
    if (!n) return {};
    std::wstring result(n, 0); MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, result.data(), n); result.resize(n-1); return result;
}
static std::wstring quote(const std::wstring &s) {
    std::wstring result = L"\""; size_t slashes = 0;
    for (wchar_t c : s) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\'); slashes = 0; result += c;
    }
    result.append(slashes * 2, L'\\'); result += L'"'; return result;
}
static void writer(AxyneTerminal *t, WindowsTerminal *p) {
    while (!t->stopping) {
        std::string data;
        { std::unique_lock<std::mutex> lock(t->queue_mutex);
          t->queue_ready.wait(lock, [&]{ return t->stopping || !t->writes.empty(); });
          if (t->stopping) break;
          data = std::move(t->writes.front()); t->writes.pop_front(); }
        size_t at = 0;
        while (at < data.size() && !t->stopping) {
            DWORD n = 0;
            if (!WriteFile(p->input, data.data()+at, static_cast<DWORD>(data.size()-at), &n, nullptr) || !n) break;
            at += n;
        }
        { std::lock_guard<std::mutex> lock(t->queue_mutex); t->queued_bytes -= data.size(); }
    }
}
static void reader(AxyneTerminal *t, WindowsTerminal *p) {
    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(p->output, nullptr, 0, nullptr, &available, nullptr)) break;
        if (available) {
            char buffer[16384]; DWORD n = 0;
            if (!ReadFile(p->output, buffer, sizeof buffer, &n, nullptr) || !n) break;
            axyne_terminal_feed(t, buffer, n);
            if (t->changed) t->changed(t, t->context);
        } else {
            // ClosePseudoConsole can return before its final output on newer
            // Windows versions. Only a broken pipe is stream EOF.
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    // Exit is serialized behind the final frame and ClosePseudoConsole.
    while (!p->closed) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    t->running = false;
    if (t->exited) t->exited(t, static_cast<int>(p->exit_code), t->context);
}
static void closer(AxyneTerminal *t, WindowsTerminal *p) {
    while (!t->stopping && WaitForSingleObject(p->process, 20) == WAIT_TIMEOUT) {}
    t->stopping = true; t->queue_ready.notify_all();
    TerminateJobObject(p->job, 1);
    // Cancel synchronous writer I/O before joining, keeping the reader alive
    // while ClosePseudoConsole emits its final frame (Microsoft's contract).
    if (t->writer.joinable()) CancelSynchronousIo(t->writer.native_handle());
    WaitForSingleObject(p->process, INFINITE);
    if (!GetExitCodeProcess(p->process, &p->exit_code)) p->exit_code = static_cast<DWORD>(-1);
    HANDLE pc;
    { std::lock_guard<std::mutex> lock(p->pc_mutex); pc = p->pc; p->pc = nullptr; }
    if (pc) p->close(pc); // No mutex held while the reader drains the last frame.
    p->closed = true;
}
AxyneStatus terminal_transport_start(AxyneTerminal *t, const AxyneTerminalSpec *s, AxyneError *e) {
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    auto create = reinterpret_cast<CreatePC>(GetProcAddress(kernel, "CreatePseudoConsole"));
    auto resize = reinterpret_cast<ResizePC>(GetProcAddress(kernel, "ResizePseudoConsole"));
    auto closepc = reinterpret_cast<ClosePC>(GetProcAddress(kernel, "ClosePseudoConsole"));
    if (!create || !resize || !closepc) return terminal_result(e, AXYNE_STATUS_UNSUPPORTED, "Real terminals require Windows 10 1809 or later (ConPTY)");
    std::wstring executable = wide(s->executable), cwd = wide(s->working_directory), command = quote(executable);
    for (size_t i = 0; i < s->argument_count; ++i) {
        if (!s->arguments[i]) return terminal_result(e, AXYNE_STATUS_INVALID_ARGUMENT);
        command += L" " + quote(wide(s->arguments[i]));
    }
    auto *p = new WindowsTerminal; p->resize = resize; p->close = closepc; t->transport = p;
    HANDLE inread = nullptr, outwrite = nullptr;
    if (!CreatePipe(&inread, &p->input, nullptr, 0) || !CreatePipe(&p->output, &outwrite, nullptr, 0)) {
        if (inread) CloseHandle(inread); if (outwrite) CloseHandle(outwrite); terminal_transport_destroy(t); return failure(e, "Cannot create ConPTY channels");
    }
    int rows, cols; { std::lock_guard<std::mutex> lock(t->model_mutex); vterm_get_size(t->vt, &rows, &cols); }
    HRESULT hr = create({static_cast<SHORT>(cols), static_cast<SHORT>(rows)}, inread, outwrite, 0, &p->pc);
    CloseHandle(inread); CloseHandle(outwrite);
    if (FAILED(hr)) { terminal_transport_destroy(t); return failure(e, "Cannot create pseudoconsole"); }
    // Start the drain thread before any startup error can close the console.
    try { t->reader = std::thread(reader, t, p); }
    catch (...) { CloseHandle(p->output); p->output = nullptr; terminal_transport_destroy(t); return failure(e, "Cannot create terminal reader"); }
    SIZE_T bytes = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<unsigned char> storage(bytes);
    STARTUPINFOEXW si{}; si.StartupInfo.cb = sizeof si;
    si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    bool initialized = InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &bytes) != 0;
    bool ready = initialized && UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, p->pc, sizeof p->pc, nullptr, nullptr);
    p->job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    ready = ready && p->job && SetInformationJobObject(p->job, JobObjectExtendedLimitInformation, &limits, sizeof limits);
    PROCESS_INFORMATION pi{};
    bool spawned = ready && CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si.StartupInfo, &pi);
    if (initialized) DeleteProcThreadAttributeList(si.lpAttributeList);
    if (!spawned) { terminal_transport_destroy(t); return failure(e, "Cannot start terminal shell"); }
    p->process = pi.hProcess;
    if (!AssignProcessToJobObject(p->job, pi.hProcess)) {
        TerminateProcess(pi.hProcess, 1); CloseHandle(pi.hThread); terminal_transport_destroy(t); return failure(e, "Cannot own terminal process tree");
    }
    t->running = true;
    try { t->writer = std::thread(writer, t, p); p->closer = std::thread(closer, t, p); }
    catch (...) { TerminateProcess(pi.hProcess, 1); CloseHandle(pi.hThread); terminal_transport_destroy(t); return failure(e, "Cannot create terminal workers"); }
    ResumeThread(pi.hThread); CloseHandle(pi.hThread); return terminal_result(e, AXYNE_STATUS_OK);
}
AxyneStatus terminal_transport_resize(AxyneTerminal *t, int rows, int cols, AxyneError *e) {
    auto *p = static_cast<WindowsTerminal *>(t->transport);
    std::lock_guard<std::mutex> lock(p->pc_mutex);
    return p->pc && FAILED(p->resize(p->pc, {static_cast<SHORT>(cols), static_cast<SHORT>(rows)})) ? failure(e, "Cannot resize pseudoconsole") : terminal_result(e, AXYNE_STATUS_OK);
}
void terminal_transport_stop(AxyneTerminal *t) {
    t->stopping = true; t->queue_ready.notify_all();
    auto *p = static_cast<WindowsTerminal *>(t->transport);
    if (p && p->job) TerminateJobObject(p->job, 1);
    if (t->writer.joinable()) CancelSynchronousIo(t->writer.native_handle());
}
void terminal_transport_destroy(AxyneTerminal *t) {
    terminal_transport_stop(t);
    auto *p = static_cast<WindowsTerminal *>(t->transport);
    if (!p) return;
    if (p->closer.joinable()) p->closer.join();
    else {
        if (p->pc) { p->close(p->pc); p->pc = nullptr; }
        p->closed = true;
    }
    if (t->reader.joinable()) t->reader.join();
    if (t->writer.joinable()) { CancelSynchronousIo(t->writer.native_handle()); t->writer.join(); }
    for (HANDLE handle : {p->input, p->output, p->process, p->job}) if (handle) CloseHandle(handle);
    delete p; t->transport = nullptr; t->running = false;
}
