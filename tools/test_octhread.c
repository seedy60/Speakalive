/* test_octhread.c - is the OneCore synthesizer usable from another thread?
 * Activate on the main (STA) thread, then synthesize from a worker (MTA). */
#include <windows.h>
#include "../src/engine.h"

int _fltused = 0x9875;
#pragma function(memset, memcpy)
void *__cdecl memset(void *d,int v,size_t n){ unsigned char*p=(unsigned char*)d; while(n--)*p++=(unsigned char)v; return d; }
void *__cdecl memcpy(void *d,const void *s,size_t n){ unsigned char*a=(unsigned char*)d; const unsigned char*b=(const unsigned char*)s; while(n--)*a++=*b++; return d; }
void *__cdecl memmove(void *d,const void *s,size_t n){ return memcpy(d,s,n); }
int   __cdecl memcmp(const void *a,const void *b,size_t n){ const unsigned char*p=(const unsigned char*)a,*q=(const unsigned char*)b; while(n--){ if(*p!=*q) return (int)*p-(int)*q; p++;q++; } return 0; }

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }

static SpeechEngine *g_e;
static HANDLE g_done;

static DWORD WINAPI Worker(LPVOID p)
{
    BOOL ok;
    (void)p;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ok = g_e->SaveToFile(g_e, "Hello from a background thread, testing cross apartment use.",
                         FALSE, "C:\\git\\Speakalive\\build\\test_thr.wav", FMT_WAV, 1);
    Out(ok ? "worker SaveToFile: OK\n" : "worker SaveToFile: FAIL\n");
    CoUninitialize();
    SetEvent(g_done);
    return 0;
}

void __cdecl WinMainCRTStartup(void)
{
    HANDLE t;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);   /* main = STA, like the app */
    g_e = OneCore_Get();
    if (!g_e->Detect()) { Out("OneCore not present\n"); ExitProcess(1); }
    g_e->Init(g_e);
    g_done = CreateEventA(NULL, FALSE, FALSE, NULL);
    t = CreateThread(NULL, 0, Worker, NULL, 0, NULL);
    WaitForSingleObject(g_done, 30000);
    if (t) CloseHandle(t);
    g_e->Shutdown(g_e);
    CoUninitialize();
    ExitProcess(0);
}
