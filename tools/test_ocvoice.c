/* test_ocvoice.c - render the same text with David and a non-en-US voice and
 * confirm the two differ (i.e. the selected voice is actually used). */
#include <windows.h>
#include "../src/engine.h"
#include "../src/util.h"

int _fltused = 0x9875;
#pragma function(memset, memcpy)
void *__cdecl memset(void *d,int v,size_t n){ unsigned char*p=(unsigned char*)d; while(n--)*p++=(unsigned char)v; return d; }
void *__cdecl memcpy(void *d,const void *s,size_t n){ unsigned char*a=(unsigned char*)d; const unsigned char*b=(const unsigned char*)s; while(n--)*a++=*b++; return d; }
void *__cdecl memmove(void *d,const void *s,size_t n){ return memcpy(d,s,n); }
int   __cdecl memcmp(const void *a,const void *b,size_t n){ const unsigned char*p=(const unsigned char*)a,*q=(const unsigned char*)b; while(n--){ if(*p!=*q) return (int)*p-(int)*q; p++;q++; } return 0; }

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }

static int FindVoice(SpeechEngine *e, const char *name)
{
    Voice *v = NULL; int n = e->GetVoices(e, &v), i;
    for (i = 0; i < n; i++) if (lstrcmpiA(v[i].name, name) == 0) return i;
    return -1;
}

static const char *TEXT = "Hello there. This is a fairly long test sentence used for comparison.";

void __cdecl WinMainCRTStartup(void)
{
    SpeechEngine *e;
    int di, gi;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    e = OneCore_Get();
    if (!e->Detect()) { Out("OneCore not present\n"); ExitProcess(1); }
    e->Init(e);

    di = FindVoice(e, "Microsoft David");
    gi = FindVoice(e, "Microsoft George");
    Out(di >= 0 ? "David found\n" : "David NOT found\n");
    Out(gi >= 0 ? "George found\n" : "George NOT found\n");

    if (di >= 0) {
        e->SetVoice(e, di);
        Out(e->SaveToFile(e, TEXT, FALSE, "C:\\git\\Speakalive\\build\\v_david.wav", FMT_WAV, 1)
            ? "david saved\n" : "david FAIL\n");
    }
    if (gi >= 0) {
        e->SetVoice(e, gi);
        Out(e->SaveToFile(e, TEXT, FALSE, "C:\\git\\Speakalive\\build\\v_george.wav", FMT_WAV, 1)
            ? "george saved\n" : "george FAIL\n");
    }
    e->Shutdown(e);
    CoUninitialize();
    ExitProcess(0);
}
