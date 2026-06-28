/* test_s4unclaim.c - can we free the audio DEVICE without destroying (and thus
 * crashing) the audio-dest COM object?  IAudio::UnClaim/Stop should release the
 * scarce device handle.  This mirrors the leak test (120 fresh dests, never
 * Release) but UnClaim+Stops each before leaking.  If every Select still works
 * (no exhaustion), then "UnClaim + leak" frees the resource AND never runs the
 * crashing destructor - the fix. */
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
static const GUID GUID_IAudio={0xf546b340,0xc743,0x11cd,{0x80,0xe5,0x00,0xaa,0x00,0x3e,0x4b,0x50}};

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }
#define MAXV 80
static GUID g_modes[MAXV]; static int g_count;
static TTSMODEINFOW g_mi;

void __cdecl WinMainCRTStartup(void){
    ITTSEnumW *en=NULL; ULONG fetched=0; char b[140];
    int i, ok=0, fail=0, firstFail=-1, gotAudio=0, gotStop=0;
    CoInitialize(NULL);
    if(FAILED(CoCreateInstance(&GUID_TTSEnumerator,NULL,CLSCTX_ALL,&GUID_ITTSEnumW,(void**)&en))||!en){ Out("no SAPI4\n"); ExitProcess(2); }
    en->lpVtbl->Reset(en);
    while(g_count<MAXV && en->lpVtbl->Next(en,1,&g_mi,&fetched)==S_OK && fetched==1) g_modes[g_count++]=g_mi.gModeID;
    wsprintfA(b,"voices: %d ; 120 switches, each UnClaim+Stop then leak the audio dest\n",g_count); Out(b);

    for(i=0;i<120;i++){
        IAudioMultiMediaDevice *audio=NULL; IAudio *ia=NULL; ITTSCentralW *c=NULL; HRESULT hr;
        int v=(g_count>0)?(i%g_count):0;
        CoCreateInstance(&GUID_MMAudioDest,NULL,CLSCTX_ALL,&GUID_IAudioMMDevice,(void**)&audio);
        if(!audio){ fail++; if(firstFail<0)firstFail=i; continue; }
        hr=en->lpVtbl->Select(en,g_modes[v],&c,(LPUNKNOWN)audio);
        if(SUCCEEDED(hr)&&c){ ok++; c->lpVtbl->Release(c); }
        else { fail++; if(firstFail<0)firstFail=i; }
        /* free the device but NOT the object: QI IAudio, Stop + UnClaim, then leak */
        if(SUCCEEDED(audio->lpVtbl->QueryInterface(audio,&GUID_IAudio,(void**)&ia))&&ia){
            gotAudio=1;
            if(SUCCEEDED(ia->lpVtbl->Stop(ia))) gotStop=1;
            ia->lpVtbl->UnClaim(ia);
            ia->lpVtbl->Release(ia);   /* release just our QI ref, not the object's main ref */
        }
        /* audio object deliberately leaked (never its final Release) */
    }
    wsprintfA(b,"QI IAudio worked=%d  Stop worked=%d\n",gotAudio,gotStop); Out(b);
    wsprintfA(b,"result: %d ok, %d failed",ok,fail); Out(b);
    if(fail){ wsprintfA(b," (first failure at switch %d)",firstFail); Out(b); }
    Out("\n");
    Out(fail==0 ? "PASS: UnClaim+leak survives 120 switches - frees the device without the crashing destructor.\n"
               : "FAIL: still exhausts - UnClaim did not free the resource.\n");
    en->lpVtbl->Release(en);
    ExitProcess(0);
}
