/* test_s4leak.c - the crash fix gives each voice selection a FRESH MMAudioDest
 * and never releases the old one (releasing crashes some voices).  This checks
 * the resulting leak is harmless under heavy switching: 120 selections, each a
 * new audio dest + Select + release the central but keep (leak) the audio.  If
 * every Select still succeeds, leaking does not exhaust device/handle resources. */
#include <windows.h>
#include "speech.h"

int _fltused = 0x9875;
#pragma function(memset, memcpy)
void *__cdecl memset(void *d,int v,size_t n){ unsigned char*p=(unsigned char*)d; while(n--)*p++=(unsigned char)v; return d; }
void *__cdecl memcpy(void *d,const void *s,size_t n){ unsigned char*a=(unsigned char*)d; const unsigned char*b=(const unsigned char*)s; while(n--)*a++=*b++; return d; }
int __cdecl memcmp(const void *a,const void *b,size_t n){ const unsigned char*x=(const unsigned char*)a,*y=(const unsigned char*)b; while(n--){ if(*x!=*y)return *x-*y; x++; y++; } return 0; }

static const GUID GUID_TTSEnumerator={0xd67c0280,0xc743,0x11cd,{0x80,0xe5,0x00,0xaa,0x00,0x3e,0x4b,0x50}};
static const GUID GUID_MMAudioDest={0xcb96b400,0xc743,0x11cd,{0x80,0xe5,0x00,0xaa,0x00,0x3e,0x4b,0x50}};
static const GUID GUID_ITTSEnumW={0x6b837b20,0x4a47,0x101b,{0x93,0x1a,0x00,0xaa,0x00,0x47,0xba,0x4f}};
static const GUID GUID_IAudioMMDevice={0xb68ad320,0xc743,0x11cd,{0x80,0xe5,0x00,0xaa,0x00,0x3e,0x4b,0x50}};

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }
#define MAXV 80
static GUID g_modes[MAXV]; static int g_count;
static TTSMODEINFOW g_mi;

void __cdecl WinMainCRTStartup(void){
    ITTSEnumW *en=NULL; ULONG fetched=0; char b[120];
    int i, ok=0, fail=0, firstFail=-1;
    CoInitialize(NULL);
    if(FAILED(CoCreateInstance(&GUID_TTSEnumerator,NULL,CLSCTX_ALL,&GUID_ITTSEnumW,(void**)&en))||!en){ Out("no SAPI4\n"); ExitProcess(2); }
    en->lpVtbl->Reset(en);
    while(g_count<MAXV && en->lpVtbl->Next(en,1,&g_mi,&fetched)==S_OK && fetched==1) g_modes[g_count++]=g_mi.gModeID;
    wsprintfA(b,"voices: %d ; simulating 120 voice switches (fresh+leaked audio dest each)\n",g_count); Out(b);

    for(i=0;i<120;i++){
        IAudioMultiMediaDevice *audio=NULL; ITTSCentralW *c=NULL; HRESULT hr;
        int v = (g_count>0) ? (i % g_count) : 0;
        CoCreateInstance(&GUID_MMAudioDest,NULL,CLSCTX_ALL,&GUID_IAudioMMDevice,(void**)&audio);   /* fresh */
        if(!audio){ fail++; if(firstFail<0)firstFail=i; continue; }
        hr=en->lpVtbl->Select(en,g_modes[v],&c,(LPUNKNOWN)audio);
        if(SUCCEEDED(hr)&&c){ ok++; c->lpVtbl->Release(c); }   /* release central, LEAK audio */
        else { fail++; if(firstFail<0)firstFail=i; }
        /* audio deliberately leaked (never Release) - exactly what the fix does */
    }
    wsprintfA(b,"result: %d ok, %d failed",ok,fail); Out(b);
    if(fail){ wsprintfA(b," (first failure at switch %d)",firstFail); Out(b); }
    Out("\n");
    Out(fail==0 ? "PASS: 120 leaked audio dests, every Select still works - leak is safe.\n"
               : "FAIL: switching broke after leaking audio dests - need another approach.\n");
    en->lpVtbl->Release(en);
    ExitProcess(0);
}
