/* test_cancel.c - verify that raising g_saveCancel aborts an in-progress
 * SaveToFile render early (rather than running to completion).
 *
 * Mirrors the app: the engine is created on the main thread, SaveToFile runs on
 * a worker, and the main thread cancels ~500 ms in.  A render of this much text
 * normally takes several seconds, so a quick FALSE return proves it aborted. */
#include <windows.h>
#include "../src/engine.h"
#include "../src/util.h"

int _fltused = 0x9875;
#pragma function(memset, memcpy)
void *__cdecl memset(void *d,int v,size_t n){ unsigned char*p=(unsigned char*)d; while(n--)*p++=(unsigned char)v; return d; }
void *__cdecl memcpy(void *d,const void *s,size_t n){ unsigned char*a=(unsigned char*)d; const unsigned char*b=(const unsigned char*)s; while(n--)*a++=*b++; return d; }
void *__cdecl memmove(void *d,const void *s,size_t n){ return memcpy(d,s,n); }
int   __cdecl memcmp(const void*a,const void*b,size_t n){ const unsigned char*p=a,*q=b; while(n--){ if(*p!=*q)return *p-*q; p++;q++; } return 0; }

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }

static char g_big[300000];
typedef struct { SpeechEngine *e; const char *path; volatile BOOL done; BOOL ret; } Job;

static DWORD WINAPI Worker(LPVOID p)
{
    Job *j = (Job *)p;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    j->ret  = j->e->SaveToFile(j->e, g_big, FALSE, j->path, FMT_WAV, 1);
    CoUninitialize();
    j->done = TRUE;
    return 0;
}

static void TestCancel(SpeechEngine *e, const char *path, const char *label)
{
    Job    j;
    HANDLE th;
    DWORD  t0, elapsed;
    char   line[160];

    j.e = e; j.path = path; j.done = FALSE; j.ret = FALSE;
    g_saveCancel = 0;
    DeleteFileA(path);

    t0 = GetTickCount();
    th = CreateThread(NULL, 0, Worker, &j, 0, NULL);
    Sleep(500);                       /* let the render get going */
    g_saveCancel = 1;                 /* user hits Cancel */
    if (e->Stop) e->Stop(e);          /* SAPI 5: purge interrupts Speak */
    WaitForSingleObject(th, 30000);
    CloseHandle(th);
    elapsed = GetTickCount() - t0;

    wsprintfA(line, "%s: returned %s after %lu ms  -> %s\n",
        label, j.ret ? "TRUE" : "FALSE", (unsigned long)elapsed,
        (!j.ret && elapsed < 2500) ? "CANCELLED EARLY (good)"
                                   : "did NOT cancel early");
    Out(line);
    DeleteFileA(path);
}

void __cdecl WinMainCRTStartup(void)
{
    const char *unit = "This is Seediffusion, a long passage repeated many times to make a big render. ";
    int ulen = lstrlenA(unit), pos = 0;
    while (pos + ulen < (int)sizeof(g_big) - 1) { lstrcpyA(g_big + pos, unit); pos += ulen; }
    g_big[pos] = 0;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    {
        SpeechEngine *e = Sapi5_Get();
        if (e->Detect()) { e->Init(e);
            TestCancel(e, "C:\\git\\Speakalive\\build\\cancel_s5.wav", "SAPI5  ");
            e->Shutdown(e); }
        else Out("SAPI5  : not present\n");
    }
    {
        SpeechEngine *oc = OneCore_Get();
        if (oc->Detect()) { Voice *v=NULL; oc->Init(oc); oc->GetVoices(oc,&v);
            TestCancel(oc, "C:\\git\\Speakalive\\build\\cancel_oc.wav", "OneCore");
            oc->Shutdown(oc); }
        else Out("OneCore: not present\n");
    }

    CoUninitialize();
    ExitProcess(0);
}
