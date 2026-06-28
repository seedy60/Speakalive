/* test_s4ludo2.c - for "Ludoviko 8000 A" (A-law), try to make Select succeed:
 *   1) plain MMAudioDest          (baseline - expected to fail 0x80040202)
 *   2) MMAudioDest told to use PCM via IAudio::WaveFormatSet before Select
 *   3) the file destination (the save path)
 * Tells us whether a simple PCM request fixes live speak, and whether saving
 * with these voices works.  Sound-free (no TextData). */
#include <windows.h>
#include <mmreg.h>
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
static const GUID GUID_AudioDestFile={0xd4623720,0xe4b9,0x11cf,{0x8d,0x56,0x00,0xa0,0xc9,0x03,0x4a,0x7e}};
static const GUID GUID_IAudioFile={0xfd7c2320,0x3d6d,0x11b9,{0xc0,0x00,0xfe,0xd6,0xcb,0xa3,0xb1,0xa9}};

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }
static TTSMODEINFOW g_mi;
static int matches(const WCHAR *s, const char *sub){ int i,j; for(i=0;s[i];i++){ for(j=0;sub[j];j++){ if((char)s[i+j]!=sub[j])break; } if(!sub[j])return 1; } return 0; }

void __cdecl WinMainCRTStartup(void){
    ITTSEnumW *en=NULL; ULONG fetched=0; char b[200];
    CoInitialize(NULL);
    if(FAILED(CoCreateInstance(&GUID_TTSEnumerator,NULL,CLSCTX_ALL,&GUID_ITTSEnumW,(void**)&en))||!en){ Out("no SAPI4\n"); ExitProcess(2); }
    en->lpVtbl->Reset(en);
    while(en->lpVtbl->Next(en,1,&g_mi,&fetched)==S_OK && fetched==1){
        const WCHAR *nmW=g_mi.szModeName[0]?g_mi.szModeName:g_mi.szProductName; char nm[80]; int i;
        ITTSCentralW *c; HRESULT hr;
        if(!matches(nmW,"8000 A") && !matches(nmW,"8000 U")) continue;
        for(i=0;nmW[i]&&i<79;i++)nm[i]=(char)nmW[i]; nm[i]=0;
        Out("== "); Out(nm); Out(" ==\n");

        /* 1) plain MMAudioDest */
        { IAudioMultiMediaDevice *a=NULL; c=NULL;
          CoCreateInstance(&GUID_MMAudioDest,NULL,CLSCTX_ALL,&GUID_IAudioMMDevice,(void**)&a);
          hr=en->lpVtbl->Select(en,g_mi.gModeID,&c,(LPUNKNOWN)a);
          wsprintfA(b,"  plain MMAudioDest:        Select hr=0x%08lx central=%s\n",(unsigned long)hr,c?"yes":"no"); Out(b);
          if(c)c->lpVtbl->Release(c); if(a)a->lpVtbl->Release(a); }

        /* 2) MMAudioDest with PCM forced via WaveFormatSet */
        { IAudioMultiMediaDevice *a=NULL; IAudio *ia=NULL; c=NULL;
          CoCreateInstance(&GUID_MMAudioDest,NULL,CLSCTX_ALL,&GUID_IAudioMMDevice,(void**)&a);
          if(a && SUCCEEDED(a->lpVtbl->QueryInterface(a,&GUID_IAudio,(void**)&ia)) && ia){
              WAVEFORMATEX wf; SDATA sd; HRESULT shr;
              ZeroMemory(&wf,sizeof(wf)); wf.wFormatTag=WAVE_FORMAT_PCM; wf.nChannels=1; wf.nSamplesPerSec=8000;
              wf.wBitsPerSample=16; wf.nBlockAlign=2; wf.nAvgBytesPerSec=16000;
              sd.pData=&wf; sd.dwSize=sizeof(wf);
              shr=ia->lpVtbl->WaveFormatSet(ia,sd);
              wsprintfA(b,"  WaveFormatSet(PCM 8k) hr=0x%08lx\n",(unsigned long)shr); Out(b);
              ia->lpVtbl->Release(ia);
          }
          hr=en->lpVtbl->Select(en,g_mi.gModeID,&c,(LPUNKNOWN)a);
          wsprintfA(b,"  MMAudioDest+PCM:          Select hr=0x%08lx central=%s\n",(unsigned long)hr,c?"yes":"no"); Out(b);
          if(c)c->lpVtbl->Release(c); if(a)a->lpVtbl->Release(a); }

        /* 3) file destination (the save path) */
        { IAudioFile *f=NULL; c=NULL;
          CoCreateInstance(&GUID_AudioDestFile,NULL,CLSCTX_ALL,&GUID_IAudioFile,(void**)&f);
          hr=en->lpVtbl->Select(en,g_mi.gModeID,&c,(LPUNKNOWN)f);
          wsprintfA(b,"  file dest (save path):    Select hr=0x%08lx central=%s\n",(unsigned long)hr,c?"yes":"no"); Out(b);
          if(c)c->lpVtbl->Release(c); if(f)f->lpVtbl->Release(f); }
    }
    en->lpVtbl->Release(en);
    ExitProcess(0);
}
