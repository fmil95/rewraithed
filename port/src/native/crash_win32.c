/*
 * Windows diagnostics: log a symbolized stack trace on a crash, and note
 * when the game exits through exit().
 */
#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "dbghelp.lib")

static void port_print_frame(HANDLE process, DWORD64 addr)
{
    char buffer[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO *sym = (SYMBOL_INFO *)buffer;
    DWORD64 disp = 0;
    IMAGEHLP_LINE64 line;
    DWORD lineDisp = 0;

    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    line.SizeOfStruct = sizeof(line);

    if (SymFromAddr(process, addr, &disp, sym))
    {
        if (SymGetLineFromAddr64(process, addr, &lineDisp, &line))
        {
            fprintf(stderr, "    %08llx %s+0x%llx  (%s:%lu)\n", addr, sym->Name, disp, line.FileName, line.LineNumber);
        }
        else
        {
            fprintf(stderr, "    %08llx %s+0x%llx\n", addr, sym->Name, disp);
        }
    }
    else
    {
        fprintf(stderr, "    %08llx ?\n", addr);
    }
}

static LONG WINAPI port_crash_filter(EXCEPTION_POINTERS *info)
{
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    CONTEXT ctx = *info->ContextRecord;
    STACKFRAME64 frame;
    int i;

    fprintf(stderr, "\n[rewraithed] CRASH: exception 0x%08lx at %p",
        info->ExceptionRecord->ExceptionCode, info->ExceptionRecord->ExceptionAddress);

    if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
    {
        fprintf(stderr, " (%s 0x%08lx)",
            info->ExceptionRecord->ExceptionInformation[0] ? "write to" : "read from",
            (unsigned long)info->ExceptionRecord->ExceptionInformation[1]);
    }

    fprintf(stderr, "\n");

    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    SymInitialize(process, NULL, TRUE);

    ZeroMemory(&frame, sizeof(frame));
    frame.AddrPC.Offset = ctx.Eip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Ebp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Esp;
    frame.AddrStack.Mode = AddrModeFlat;

    for (i = 0; i < 32; i++)
    {
        if (!StackWalk64(IMAGE_FILE_MACHINE_I386, process, thread, &frame, &ctx, NULL,
                SymFunctionTableAccess64, SymGetModuleBase64, NULL) || frame.AddrPC.Offset == 0)
        {
            break;
        }

        port_print_frame(process, frame.AddrPC.Offset);
    }

    fflush(stderr);

    return EXCEPTION_EXECUTE_HANDLER;
}

/* --- hang watchdog ------------------------------------------------------
 * If no frame is presented for REWRAITHED_WATCHDOG seconds, print the main
 * thread's stack (once per hang). Enabled only when the variable is set.
 * REWRAITHED_STACKAT=<seconds> prints it once at that time, for stalls in
 * which frames are still presented. */

static int watchdog_stack_at;

static HANDLE watchdog_target;
static volatile LONG watchdog_frames;

void port_watchdog_kick(void)
{
    InterlockedIncrement(&watchdog_frames);
}

/* While the thread is suspended nothing that may take a lock (heap, stdio,
 * dbghelp) is called: frames are collected by walking the EBP chain with
 * ReadProcessMemory, and symbolized only after the thread is resumed. */
static void dump_thread_stack(HANDLE thread)
{
    HANDLE process = GetCurrentProcess();
    CONTEXT ctx;
    DWORD frames[24];
    int count = 0;
    int i;

    SuspendThread(thread);

    ZeroMemory(&ctx, sizeof(ctx));
    ctx.ContextFlags = CONTEXT_FULL;

    if (GetThreadContext(thread, &ctx))
    {
        DWORD ebp = ctx.Ebp;

        frames[count++] = ctx.Eip;

        while (count < 24)
        {
            DWORD pair[2];
            SIZE_T got = 0;

            if (!ReadProcessMemory(process, (void *)(uintptr_t)ebp, pair, sizeof(pair), &got) || (got != sizeof(pair)) ||
                (pair[1] == 0) || (pair[0] <= ebp))
            {
                break;
            }

            frames[count++] = pair[1];
            ebp = pair[0];
        }
    }

    ResumeThread(thread);

    for (i = 0; i < count; i++)
    {
        port_print_frame(process, frames[i]);
    }

    fflush(stderr);
}

static DWORD WINAPI watchdog_main(void *param)
{
    int seconds = (int)(intptr_t)param;
    LONG last = -1;
    int idle = 0;
    int reported = 0;
    int elapsed = 0;

    for (;;)
    {
        LONG now;

        Sleep(1000);

        if (++elapsed == watchdog_stack_at)
        {
            fprintf(stderr, "\n[rewraithed] STACKAT: main thread at %d s (frame %ld):\n", elapsed, watchdog_frames);
            dump_thread_stack(watchdog_target);
        }

        if (seconds <= 0)
        {
            continue;
        }

        now = watchdog_frames;

        if (now != last)
        {
            last = now;
            idle = 0;
            reported = 0;
            continue;
        }

        if ((++idle >= seconds) && !reported)
        {
            HANDLE snap;
            THREADENTRY32 te;

            fprintf(stderr, "\n[rewraithed] WATCHDOG: no frame for %d s (frame %ld), main thread:\n", idle, now);
            dump_thread_stack(watchdog_target);

            snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            te.dwSize = sizeof(te);

            if (Thread32First(snap, &te))
            {
                do
                {
                    if ((te.th32OwnerProcessID == GetCurrentProcessId()) &&
                        (te.th32ThreadID != GetCurrentThreadId()) &&
                        (te.th32ThreadID != GetThreadId(watchdog_target)))
                    {
                        HANDLE t = OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID);

                        if (t != NULL)
                        {
                            fprintf(stderr, "  thread %lu:\n", te.th32ThreadID);
                            dump_thread_stack(t);
                            CloseHandle(t);
                        }
                    }
                } while (Thread32Next(snap, &te));
            }

            CloseHandle(snap);
            reported = 1;
        }
    }
}

static void port_start_watchdog(void)
{
    const char *env = getenv("REWRAITHED_WATCHDOG");
    const char *at = getenv("REWRAITHED_STACKAT");
    int seconds = (env != NULL) ? atoi(env) : 0;

    watchdog_stack_at = (at != NULL) ? atoi(at) : 0;

    if ((seconds <= 0) && (watchdog_stack_at <= 0))
    {
        return;
    }

    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &watchdog_target,
        0, FALSE, DUPLICATE_SAME_ACCESS);

    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    SymInitialize(GetCurrentProcess(), NULL, TRUE);

    CreateThread(NULL, 0, watchdog_main, (void *)(intptr_t)seconds, 0, NULL);
}

static void port_on_exit(void)
{
    fprintf(stderr, "[rewraithed] exit() called\n");
    fflush(stderr);
}

/* --- integer division as on the R3000 ------------------------------------
 * A MIPS div/divu by zero does not trap: LO gets -1 (or 1 for a negative
 * signed dividend) and HI the dividend; INT_MIN / -1 gives INT_MIN and 0.
 * The game's compiler added no zero checks, and some code relies on that
 * (e.g. the sound sequencer setting a tempo from a slot not set up yet).
 * x86 faults instead, so the fault on a 32-bit div/idiv in the game's code
 * is turned into the MIPS result in EAX (quotient) and EDX (remainder). */

static unsigned char *exe_start;
static unsigned char *exe_end;

/* bytes taken by a ModRM byte and what follows it (32-bit addressing) */
static int modrm_length(const unsigned char *p)
{
    int mod = p[0] >> 6;
    int rm = p[0] & 7;
    int len = 1;

    if (mod == 3)
    {
        return 1;
    }

    if (rm == 4)
    {
        len++;

        if ((mod == 0) && ((p[1] & 7) == 5))
        {
            len += 4;
        }
    }
    else if ((mod == 0) && (rm == 5))
    {
        return len + 4;
    }

    if (mod == 1)
    {
        len += 1;
    }
    else if (mod == 2)
    {
        len += 4;
    }

    return len;
}

static LONG CALLBACK port_division_handler(EXCEPTION_POINTERS *ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    CONTEXT *c = ep->ContextRecord;
    const unsigned char *ip = (const unsigned char *)(uintptr_t)c->Eip;
    int op;
    DWORD dividend;

    if ((code != EXCEPTION_INT_DIVIDE_BY_ZERO) && (code != EXCEPTION_INT_OVERFLOW))
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if ((ip < exe_start) || (ip >= exe_end) || (ip[0] != 0xF7))
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    op = (ip[1] >> 3) & 7;
    dividend = c->Eax;

    if (op == 6)            /* div */
    {
        c->Eax = 0xFFFFFFFF;
        c->Edx = dividend;
    }
    else if (op == 7)       /* idiv */
    {
        if (code == EXCEPTION_INT_OVERFLOW)
        {
            c->Eax = 0x80000000;
            c->Edx = 0;
        }
        else
        {
            c->Eax = ((LONG)dividend < 0) ? 1 : 0xFFFFFFFF;
            c->Edx = dividend;
        }
    }
    else
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    c->Eip += 1 + modrm_length(ip + 1);

    return EXCEPTION_CONTINUE_EXECUTION;
}

void port_install_crash_handler(void)
{
    HMODULE exe = GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((unsigned char *)exe + ((IMAGE_DOS_HEADER *)exe)->e_lfanew);

    exe_start = (unsigned char *)exe;
    exe_end = exe_start + nt->OptionalHeader.SizeOfImage;
    AddVectoredExceptionHandler(1, port_division_handler);

    SetUnhandledExceptionFilter(port_crash_filter);
    atexit(port_on_exit);
    port_start_watchdog();
}

#else

void port_install_crash_handler(void)
{
}

void port_watchdog_kick(void)
{
}

#endif
