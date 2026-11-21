// Debug aid for the 64-bit port (new code, not recovered): writes openrfg_crash.txt (next to where the program was
// started) with the function, file and line of the first access violations, including ones before WinMain.
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <exception>
#include <stdint.h>
#include <string.h>

#pragma init_seg(compiler)

namespace
{
    volatile LONG Crash_count = 0;

    void put(HANDLE file, const char* text)
    {
        DWORD written = 0;
        WriteFile(file, text, (DWORD)strlen(text), &written, nullptr);
    }

    // ---- write watch (debug aid): openrfg_watch(p, n) makes the page of p read-only; a write into [p, p+n) is logged with
    // its stack, writes elsewhere in the page are let through (single-stepped, then the page is protected again).
    char* Watch_addr = nullptr;
    size_t Watch_len = 0;
    DWORD64 Watch_page = 0;
    bool Watch_active = false;
    // Per thread: the thread whose write was let through re-protects the page at its single step. One flag for all
    // threads lost a re-arm when two threads wrote into the page at once, and the second single step went unhandled
    // (the process ended with 0x80000004).
    thread_local bool Watch_rearm = false;

    // Set while reporting from the loader's process detach: dbghelp may load modules, which would deadlock there.
    bool Report_raw_only = false;

    void report(EXCEPTION_POINTERS* info, const char* title)
    {
        HANDLE file = CreateFileA("openrfg_crash.txt", FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return;
        }
        char line[512];
        const EXCEPTION_RECORD* rec = info->ExceptionRecord;
        snprintf(line, sizeof(line), "=== %s exception 0x%08lX at %p", title, rec->ExceptionCode, rec->ExceptionAddress);
        put(file, line);
        if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2)
        {
            snprintf(line, sizeof(line), " (%s address %p)", rec->ExceptionInformation[0] == 0 ? "read" : rec->ExceptionInformation[0] == 1 ? "write" : "execute", (void*)rec->ExceptionInformation[1]);
            put(file, line);
        }
        put(file, "\r\n");
#ifdef _M_X64
        {
            // Raw info first (no dbghelp): module base, registers and return-address-like values on the stack.
            const unsigned char* base = (const unsigned char*)GetModuleHandleA(nullptr);
            const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + ((const IMAGE_DOS_HEADER*)base)->e_lfanew);
            const unsigned char* end = base + nt->OptionalHeader.SizeOfImage;
            snprintf(line, sizeof(line), "  exe base=%p rip_off=0x%llX rsp=%p\r\n", (const void*)base, (unsigned long long)((const unsigned char*)info->ContextRecord->Rip - base), (void*)info->ContextRecord->Rsp);
            put(file, line);
            const unsigned long long* sp = (const unsigned long long*)info->ContextRecord->Rsp;
            int found = 0;
            __try
            {
                for (int k = 0; k < 512 && found < 24; ++k)
                {
                    const unsigned char* v = (const unsigned char*)sp[k];
                    if (v >= base && v < end)
                    {
                        snprintf(line, sizeof(line), "  raw exe+0x%llX\r\n", (unsigned long long)(v - base));
                        put(file, line);
                        ++found;
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
        if (!Report_raw_only)
        __try
        {
        HANDLE process = GetCurrentProcess();
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        SymInitialize(process, nullptr, TRUE);
        CONTEXT context = *info->ContextRecord;
        STACKFRAME64 frame = {};
        frame.AddrPC.Offset = context.Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = context.Rsp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = context.Rsp;
        frame.AddrStack.Mode = AddrModeFlat;
        for (int i = 0; i < 40; ++i)
        {
            char buffer[sizeof(SYMBOL_INFO) + 256] = {};
            SYMBOL_INFO* symbol = (SYMBOL_INFO*)buffer;
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 255;
            DWORD64 displacement = 0;
            const char* name = "?";
            if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol))
            {
                name = symbol->Name;
            }
            IMAGEHLP_LINE64 lineInfo = {};
            lineInfo.SizeOfStruct = sizeof(lineInfo);
            DWORD lineDisplacement = 0;
            if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisplacement, &lineInfo))
            {
                snprintf(line, sizeof(line), "  #%d %s  [%s:%lu]\r\n", i, name, lineInfo.FileName, lineInfo.LineNumber);
            }
            else
            {
                snprintf(line, sizeof(line), "  #%d %s  [0x%llX]\r\n", i, name, (unsigned long long)frame.AddrPC.Offset);
            }
            put(file, line);
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr) || frame.AddrPC.Offset == 0)
            {
                break;
            }
        }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            put(file, "  (symbolizing failed)\r\n");
        }
#endif
        CloseHandle(file);
    }

    DWORD WINAPI report_thread(LPVOID param)
    {
        report((EXCEPTION_POINTERS*)param, "CRASH (stack overflow)");
        return 0;
    }

    // Small-stack first line for a stack overflow: no snprintf, just hex digits.
    void stack_overflow_line(EXCEPTION_POINTERS* info)
    {
        char line[96] = "=== STACK OVERFLOW rip_off=0x";
        const unsigned char* base = (const unsigned char*)GetModuleHandleA(nullptr);
#ifdef _M_X64
        unsigned long long v = (unsigned long long)((const unsigned char*)info->ContextRecord->Rip - base);
#else
        unsigned long long v = (unsigned long long)((const unsigned char*)info->ContextRecord->Eip - base);
#endif
        char* p = line + strlen(line);
        for (int shift = 60; shift >= 0; shift -= 4)
        {
            *p++ = "0123456789ABCDEF"[(v >> shift) & 0xF];
        }
        memcpy(p, " thread=", 8);
        p += 8;
        unsigned long tid = GetCurrentThreadId();
        for (int shift = 28; shift >= 0; shift -= 4)
        {
            *p++ = "0123456789ABCDEF"[(tid >> shift) & 0xF];
        }
        *p++ = '\r';
        *p++ = '\n';
        *p = 0;
        HANDLE file = CreateFileA("openrfg_crash.txt", FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            put(file, line);
            CloseHandle(file);
        }
    }

    LONG CALLBACK early_crash_handler(EXCEPTION_POINTERS* info)
    {
        const EXCEPTION_RECORD* rec = info->ExceptionRecord;
        const DWORD code = rec->ExceptionCode;
        DWORD oldProtect;
        if (Watch_rearm && code == EXCEPTION_SINGLE_STEP)
        {
            Watch_rearm = false;
            VirtualProtect((void*)Watch_page, 4096, PAGE_READONLY, &oldProtect);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        if (Watch_active && code == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2 && rec->ExceptionInformation[0] == 1 && (rec->ExceptionInformation[1] & ~(ULONG_PTR)4095) == Watch_page)
        {
            const char* hit = (const char*)rec->ExceptionInformation[1];
            VirtualProtect((void*)Watch_page, 4096, PAGE_READWRITE, &oldProtect);
            if (hit >= Watch_addr && hit < Watch_addr + Watch_len)
            {
                Watch_active = false;
                report(info, "WATCH HIT (first write into the watched item)");
            }
            else
            {
                Watch_rearm = true;
                info->ContextRecord->EFlags |= 0x100;
            }
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        if (code == 0xE0DB0001u)
        {
            report(info, "TRACE (not a crash)");
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_ILLEGAL_INSTRUCTION && !((code & 0xF0000000u) == 0xC0000000u && code != 0xC0000005u && code != 0xC000013Au && code != 0xC0020043u && code != 0xC0000008u))
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        if (InterlockedIncrement(&Crash_count) > 4)
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        if (code == EXCEPTION_STACK_OVERFLOW)
        {
            // The overflowed stack has only the guaranteed reserve left, too little for dbghelp: a second overflow
            // inside the handler ends the process with no report at all. Write one small line here, then build the
            // report on a fresh thread with its own stack.
            stack_overflow_line(info);
            HANDLE helper = CreateThread(nullptr, 0x100000, report_thread, info, 0, nullptr);
            if (helper != nullptr)
            {
                WaitForSingleObject(helper, 10000);
                CloseHandle(helper);
            }
            return EXCEPTION_CONTINUE_SEARCH;
        }
        report(info, "CRASH");
        return EXCEPTION_CONTINUE_SEARCH;
    }

    void logline(const char* t){ HANDLE f=CreateFileA("openrfg_crash.txt",FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr); if(f!=INVALID_HANDLE_VALUE){put(f,t);put(f,"\r\n");CloseHandle(f);} }
    LONG WINAPI late_filter(EXCEPTION_POINTERS* i){ report(i,"UNHANDLED"); return EXCEPTION_CONTINUE_SEARCH; }
    void abort_handler(int){ logline("=== abort() called"); TerminateProcess(GetCurrentProcess(),3); }
    void terminate_handler(){ logline("=== std::terminate called (uncaught C++ exception)"); TerminateProcess(GetCurrentProcess(),3); }
    void invalid_handler(const wchar_t*,const wchar_t*,const wchar_t*,unsigned,uintptr_t){ logline("=== invalid CRT parameter"); TerminateProcess(GetCurrentProcess(),3); }
    void exit_handler(){ logline("=== process exiting (exit() called)"); }

#ifdef _M_X64
    // ---- process end log (debug aid): a TLS callback sees ExitProcess (also reached by exit and _exit), a patch on
    // KERNELBASE!TerminateProcess sees a self-terminate. Each writes the caller's stack, so a quit with no crash
    // report still names who ended the process.
    typedef LONG(NTAPI* nt_terminate_process_fn)(HANDLE, LONG);
    nt_terminate_process_fn Nt_terminate_process = nullptr;
    volatile LONG Exit_reported = 0;

    void report_here(const char* title, bool raw_only)
    {
        CONTEXT context = {};
        RtlCaptureContext(&context);
        EXCEPTION_RECORD record = {};
        record.ExceptionAddress = (void*)context.Rip;
        EXCEPTION_POINTERS pointers = { &record, &context };
        const bool old_raw_only = Report_raw_only;
        Report_raw_only = raw_only;
        report(&pointers, title);
        Report_raw_only = old_raw_only;
    }

    BOOL WINAPI terminate_process_hook(HANDLE process, UINT exit_code)
    {
        if (GetProcessId(process) == GetCurrentProcessId() && InterlockedIncrement(&Exit_reported) == 1)
        {
            char line[96];
            snprintf(line, sizeof(line), "=== TerminateProcess(self, %u) called", exit_code);
            logline(line);
            report_here("TerminateProcess caller", false);
        }
        return Nt_terminate_process(process, (LONG)exit_code) >= 0;
    }

    void install_terminate_process_hook()
    {
        Nt_terminate_process = (nt_terminate_process_fn)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtTerminateProcess");
        HMODULE kernelbase = GetModuleHandleA("kernelbase.dll");
        unsigned char* target = kernelbase != nullptr ? (unsigned char*)GetProcAddress(kernelbase, "TerminateProcess") : nullptr;
        if (Nt_terminate_process == nullptr || target == nullptr)
        {
            return;
        }
        // jmp qword ptr [rip+0] followed by the absolute address (14 bytes).
        unsigned char patch[14] = { 0xFF, 0x25, 0, 0, 0, 0 };
        const unsigned long long hook = (unsigned long long)&terminate_process_hook;
        memcpy(patch + 6, &hook, 8);
        DWORD old_protect;
        if (VirtualProtect(target, sizeof(patch), PAGE_EXECUTE_READWRITE, &old_protect))
        {
            memcpy(target, patch, sizeof(patch));
            VirtualProtect(target, sizeof(patch), old_protect, &old_protect);
            FlushInstructionCache(GetCurrentProcess(), target, sizeof(patch));
        }
    }
#endif

    struct early_crash_setup
    {
        early_crash_setup()
        {
            DeleteFileA("openrfg_crash.txt"); // each run starts a fresh file
            AddVectoredExceptionHandler(1, early_crash_handler);
            // Stack kept back for the handler on the main thread (keen threads do this in their entry).
            ULONG stack_guarantee = 0x10000;
            SetThreadStackGuarantee(&stack_guarantee);
            SetUnhandledExceptionFilter(late_filter);
            signal(SIGABRT, abort_handler);
            set_terminate(terminate_handler);
            _set_invalid_parameter_handler(invalid_handler);
            atexit(exit_handler);
#ifdef _M_X64
            install_terminate_process_hook();
#endif
        }
    };
    early_crash_setup Early_crash_setup;
}

#ifdef _M_X64
static void NTAPI openrfg_exit_tls_callback(PVOID, DWORD reason, PVOID)
{
    if (reason == DLL_PROCESS_DETACH && InterlockedIncrement(&Exit_reported) == 1)
    {
        logline("=== process ending through ExitProcess (exit, _exit or ExitProcess); raw frames below name the caller");
        report_here("ExitProcess caller", true);
    }
}
#pragma comment(linker, "/INCLUDE:_tls_used")
#pragma comment(linker, "/INCLUDE:openrfg_exit_tls_callback_entry")
#pragma const_seg(".CRT$XLB")
extern "C" const PIMAGE_TLS_CALLBACK openrfg_exit_tls_callback_entry = openrfg_exit_tls_callback;
#pragma const_seg()
#endif

void openrfg_watch(void* p, size_t n)
{
    DWORD oldProtect;
    Watch_addr = (char*)p;
    Watch_len = n;
    Watch_page = (DWORD64)p & ~(DWORD64)4095;
    Watch_active = true;
    VirtualProtect((void*)Watch_page, 4096, PAGE_READONLY, &oldProtect);
}

// Debug aid: appends one line to openrfg_crash.txt.
void openrfg_trace_here()
{
    RaiseException(0xE0DB0001u, 0, 0, nullptr);
}

void openrfg_log(const char* text)
{
    HANDLE file = CreateFileA("openrfg_crash.txt", FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        put(file, text);
        put(file, "\r\n");
        CloseHandle(file);
    }
}

// ---- hang watchdog (debug aid): rfg_do_frame calls openrfg_heartbeat() every frame; if no frame completes for 6 seconds
// the main thread's stack is written to openrfg_crash.txt as "HANG" (up to 3 times).
namespace
{
    volatile LONG Heartbeat = 0;
    HANDLE Main_thread = nullptr;
    volatile LONG Watchdog_started = 0;

#ifdef _M_X64
    // On the first HANG, the thread rfg_do_frame runs on is often only waiting for another thread (the frame handshake),
    // so every other thread's stack is written too, one at a time, named when the thread has a description.
    void report_other_threads()
    {
        typedef HRESULT(WINAPI * get_description_fn)(HANDLE, PWSTR*);
        const get_description_fn get_description = (get_description_fn)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetThreadDescription");
        const DWORD pid = GetCurrentProcessId();
        const DWORD self = GetCurrentThreadId();
        const DWORD main_id = GetThreadId(Main_thread);
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
        {
            return;
        }
        THREADENTRY32 entry;
        entry.dwSize = sizeof(entry);
        int count = 0;
        for (BOOL ok = Thread32First(snapshot, &entry); ok && count < 64; ok = Thread32Next(snapshot, &entry))
        {
            if (entry.th32OwnerProcessID != pid || entry.th32ThreadID == self || entry.th32ThreadID == main_id)
            {
                continue;
            }
            HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
            if (thread == nullptr)
            {
                continue;
            }
            char name[128] = "";
            PWSTR wide = nullptr;
            if (get_description != nullptr && SUCCEEDED(get_description(thread, &wide)) && wide != nullptr)
            {
                WideCharToMultiByte(CP_UTF8, 0, wide, -1, name, sizeof(name), nullptr, nullptr);
                LocalFree(wide);
            }
            char title[200];
            snprintf(title, sizeof(title), "HANG other thread %lu '%s'", entry.th32ThreadID, name);
            if (SuspendThread(thread) != (DWORD)-1)
            {
                CONTEXT ctx;
                memset(&ctx, 0, sizeof(ctx));
                ctx.ContextFlags = CONTEXT_FULL;
                if (GetThreadContext(thread, &ctx))
                {
                    EXCEPTION_RECORD rec;
                    memset(&rec, 0, sizeof(rec));
                    rec.ExceptionCode = 0xE0DB0002u;
                    rec.ExceptionAddress = (PVOID)ctx.Rip;
                    EXCEPTION_POINTERS ep;
                    ep.ExceptionRecord = &rec;
                    ep.ContextRecord = &ctx;
                    report(&ep, title);
                    ++count;
                }
                ResumeThread(thread);
            }
            CloseHandle(thread);
        }
        CloseHandle(snapshot);
    }
#endif

    DWORD WINAPI watchdog_proc(LPVOID)
    {
        LONG last = -1;
        int stalled = 0;
        int reports = 0;
        for (;;)
        {
            Sleep(1000);
            LONG now = Heartbeat;
            if (now != last) { last = now; stalled = 0; continue; }
            if (++stalled < 6 || reports >= 3) continue;
            if (SuspendThread(Main_thread) == (DWORD)-1) continue;
            CONTEXT ctx;
            memset(&ctx, 0, sizeof(ctx));
            ctx.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(Main_thread, &ctx))
            {
                EXCEPTION_RECORD rec;
                memset(&rec, 0, sizeof(rec));
                rec.ExceptionCode = 0xE0DB0002u;
#ifdef _M_X64
                rec.ExceptionAddress = (PVOID)ctx.Rip;
#endif
                EXCEPTION_POINTERS ep;
                ep.ExceptionRecord = &rec;
                ep.ContextRecord = &ctx;
                report(&ep, "HANG (main thread, no frame for 6s)");
                ++reports;
            }
            ResumeThread(Main_thread);
#ifdef _M_X64
            if (reports == 1)
            {
                report_other_threads();
            }
#endif
            stalled = 3; // report again 3 seconds later
        }
    }
}

void openrfg_heartbeat()
{
    InterlockedIncrement(&Heartbeat);
    if (InterlockedCompareExchange(&Watchdog_started, 1, 0) == 0)
    {
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &Main_thread, 0, FALSE, DUPLICATE_SAME_ACCESS);
        CreateThread(nullptr, 0, watchdog_proc, nullptr, 0, nullptr);
    }
}
