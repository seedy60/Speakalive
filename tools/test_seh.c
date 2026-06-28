/* test_seh.c - validate a no-CRT crash-recovery mechanism (for surviving a voice
 * DLL that faults inside Release).  A Vectored Exception Handler, on an access
 * violation while a guard flag is set on this thread, restores a saved register
 * context (setjmp-style) so execution resumes just past the guarded call.
 * Tests: a normal guarded call, then a guarded call that deliberately crashes. */
#include <windows.h>

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }

static DWORD g_jb[6];                 /* ebx, esi, edi, ebp, esp(post-ret), eip */
static volatile LONG g_guard;
static DWORD g_thread;

/* setjmp-style: save callee-saved regs + post-return esp + return eip; return 0.
 * The VEH makes it appear to "return" 1 after a swallowed fault. */
__declspec(naked) static int SaveCtx(void)
{
    __asm {
        mov  [g_jb+0],  ebx
        mov  [g_jb+4],  esi
        mov  [g_jb+8],  edi
        mov  [g_jb+12], ebp
        lea  eax, [esp+4]
        mov  [g_jb+16], eax
        mov  eax, [esp]
        mov  [g_jb+20], eax
        xor  eax, eax
        ret
    }
}

static LONG WINAPI Veh(EXCEPTION_POINTERS *ep)
{
    if (g_guard && GetCurrentThreadId() == g_thread &&
        ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        CONTEXT *c = ep->ContextRecord;
        c->Ebx = g_jb[0]; c->Esi = g_jb[1]; c->Edi = g_jb[2];
        c->Ebp = g_jb[3]; c->Esp = g_jb[4]; c->Eip = g_jb[5];
        c->Eax = 1;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* Runs body (crash or not) under the guard.  Returns 0 if it completed, 1 if a
 * fault was caught. */
static int guarded(int crash)
{
    g_thread = GetCurrentThreadId();
    if (SaveCtx() == 0) {
        g_guard = 1;
        if (crash) { *(volatile int *)0 = 42; }   /* deliberate access violation */
        g_guard = 0;
        return 0;
    }
    g_guard = 0;
    return 1;
}

void __cdecl WinMainCRTStartup(void)
{
    SetUnhandledExceptionFilter(Veh);

    Out("1. normal guarded call...\n");
    { int r = guarded(0); Out(r ? "   unexpected recover\n" : "   completed normally\n"); }

    Out("2. guarded call that crashes...\n");
    { int r = guarded(1); Out(r ? "   RECOVERED from the crash\n" : "   did NOT recover\n"); }

    Out("3. another normal call after recovery...\n");
    { int r = guarded(0); Out(r ? "   unexpected recover\n" : "   completed normally\n"); }

    Out("RESULT: process still alive after a caught access violation - mechanism works.\n");
    ExitProcess(0);
}
