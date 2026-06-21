/* test_oclife.c - exercise the OneCore worker lifecycle: speak, supersede,
 * stop, and clean shutdown.  Passes if it finishes without hanging/crashing. */
#include <windows.h>
#include "../src/engine.h"

int _fltused = 0x9875;
#pragma function(memset, memcpy)
void *__cdecl memset(void *d,int v,size_t n){ unsigned char*p=(unsigned char*)d; while(n--)*p++=(unsigned char)v; return d; }
void *__cdecl memcpy(void *d,const void *s,size_t n){ unsigned char*a=(unsigned char*)d; const unsigned char*b=(const unsigned char*)s; while(n--)*a++=*b++; return d; }
void *__cdecl memmove(void *d,const void *s,size_t n){ return memcpy(d,s,n); }
int   __cdecl memcmp(const void *a,const void *b,size_t n){ const unsigned char*p=(const unsigned char*)a,*q=(const unsigned char*)b; while(n--){ if(*p!=*q) return (int)*p-(int)*q; p++;q++; } return 0; }

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }

void __cdecl WinMainCRTStartup(void)
{
    SpeechEngine *e;
    DWORD t0;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    e = OneCore_Get();
    if (!e->Detect()) { Out("OneCore not present\n"); ExitProcess(1); }
    e->Init(e);

    Out("speak 1 (returns immediately if non-blocking)...\n");
    t0 = GetTickCount();
    e->Speak(e, "This is a long first sentence used to check that the call to speak "
                "returns right away instead of blocking the caller while the audio "
                "is synthesised in the background.", FALSE, NULL);
    { char b[64]; wsprintfA(b, "  Speak() returned after %lu ms\n", GetTickCount() - t0); Out(b); }
    Sleep(1500);

    Out("speak 2 (supersedes 1)...\n");
    e->Speak(e, "Second utterance, superseding the first one.", FALSE, NULL);
    Sleep(1500);

    Out("stop...\n");
    e->Stop(e);
    Sleep(300);

    Out("speak 3 then immediate shutdown...\n");
    e->Speak(e, "Third utterance, then we shut down while it is still going.", FALSE, NULL);
    Sleep(200);
    e->Shutdown(e);

    Out("DONE (no hang, clean shutdown)\n");
    CoUninitialize();
    ExitProcess(0);
}
