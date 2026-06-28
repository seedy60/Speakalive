/* test_s4reuse.c - verify the crash fix's core assumption: one MMAudioDest can
 * be reused for many successive Select() calls (central released between them,
 * audio dest kept).  If every Select still yields a central, reuse is safe and
 * voice switching won't break.  (Audio output itself is checked by hand via F5.) */
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
    ITTSEnumW *en=NULL; IAudioMultiMediaDevice *audio=NULL; ULONG fetched=0; char b[120];
    int i, ok=0, fail=0; int order[8];
    CoInitialize(NULL);
    if(FAILED(CoCreateInstance(&GUID_TTSEnumerator,NULL,CLSCTX_ALL,&GUID_ITTSEnumW,(void**)&en))||!en){ Out("no SAPI4\n"); ExitProcess(2); }
    en->lpVtbl->Reset(en);
    while(g_count<MAXV && en->lpVtbl->Next(en,1,&g_mi,&fetched)==S_OK && fetched==1) g_modes[g_count++]=g_mi.gModeID;
    wsprintfA(b,"voices: %d\n",g_count); Out(b);

    /* one audio dest, created once, reused for every Select */
    CoCreateInstance(&GUID_MMAudioDest,NULL,CLSCTX_ALL,&GUID_IAudioMMDevice,(void**)&audio);
    if(!audio){ Out("MMAudioDest create failed\n"); ExitProcess(3); }

    /* visit first voice, last voice, voice 0 again, then a few - reusing audio */
    order[0]=0; order[1]=g_count>1?g_count-1:0; order[2]=0;
    order[3]=g_count>2?g_count/2:0; order[4]=0; order[5]=g_count>1?1:0;
    for(i=0;i<6;i++){ int v=order[i]; ITTSCentralW *c=NULL;
        HRESULT hr=en->lpVtbl->Select(en,g_modes[v],&c,(LPUNKNOWN)audio);
        if(SUCCEEDED(hr)&&c){ ok++; c->lpVtbl->Release(c); }   /* release central, KEEP audio */
        else { fail++; wsprintfA(b,"  Select voice %d FAILED hr=0x%08lx\n",v,(unsigned long)hr); Out(b); }
    }
    wsprintfA(b,"reused-audio Selects: %d ok, %d failed\n",ok,fail); Out(b);
    Out(fail==0 ? "RESULT: MMAudioDest reuse works - safe to keep one and not release per switch.\n"
               : "RESULT: reuse FAILED - need a different approach.\n");
    /* deliberately do NOT release audio (that is the whole point of the fix) */
    en->lpVtbl->Release(en);
    ExitProcess(0);
}
