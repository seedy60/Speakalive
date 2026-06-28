/* test_s4ludo.c - diagnose why "Ludoviko 8000 A"/"8000 U" fail to live-speak.
 * For each Ludoviko voice (and Sam as a control) it Selects with a real speaker
 * dest, reads the wave format the engine wants, and feeds one short word -
 * reporting Select hr, the format tag/rate, and TextData hr.  formatTag 1=PCM,
 * 6=A-law, 7=mu-law; A-law/mu-law that waveOut cannot play is the suspect. */
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
static const GUID GUID_ITTSBufNotifySink={0xe4963d40,0xc743,0x11cd,{0x80,0xe5,0x00,0xaa,0x00,0x3e,0x4b,0x50}};

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }
static TTSMODEINFOW g_mi;

static int matches(const WCHAR *s, const char *sub){
    int i,j; for(i=0;s[i];i++){ for(j=0;sub[j];j++){ if((char)s[i+j]!=sub[j]) break; } if(!sub[j]) return 1; } return 0;
}

void __cdecl WinMainCRTStartup(void){
    ITTSEnumW *en=NULL; ULONG fetched=0; char b[200];
    CoInitialize(NULL);
    if(FAILED(CoCreateInstance(&GUID_TTSEnumerator,NULL,CLSCTX_ALL,&GUID_ITTSEnumW,(void**)&en))||!en){ Out("no SAPI4\n"); ExitProcess(2); }
    en->lpVtbl->Reset(en);
    while(en->lpVtbl->Next(en,1,&g_mi,&fetched)==S_OK && fetched==1){
        const WCHAR *nmW=g_mi.szModeName[0]?g_mi.szModeName:g_mi.szProductName; char nm[80]; int i;
        IAudioMultiMediaDevice *audio=NULL; ITTSCentralW *c=NULL; IAudio *ia=NULL; HRESULT hr;
        if(!(matches(nmW,"Ludoviko") || matches(nmW,"Sam"))) continue;
        for(i=0;nmW[i]&&i<79;i++)nm[i]=(char)nmW[i]; nm[i]=0;

        CoCreateInstance(&GUID_MMAudioDest,NULL,CLSCTX_ALL,&GUID_IAudioMMDevice,(void**)&audio);
        hr=en->lpVtbl->Select(en,g_mi.gModeID,&c,(LPUNKNOWN)audio);
        wsprintfA(b,"%-22s Select hr=0x%08lx central=%s",nm,(unsigned long)hr,c?"yes":"NO"); Out(b);

        if(c && SUCCEEDED(c->lpVtbl->QueryInterface(c,&GUID_IAudio,(void**)&ia)) && ia){
            SDATA sd; ZeroMemory(&sd,sizeof(sd));
            if(SUCCEEDED(ia->lpVtbl->WaveFormatGet(ia,&sd)) && sd.pData){
                WAVEFORMATEX *wf=(WAVEFORMATEX*)sd.pData;
                wsprintfA(b,"  fmt tag=%u rate=%lu bits=%u ch=%u",(unsigned)wf->wFormatTag,(unsigned long)wf->nSamplesPerSec,(unsigned)wf->wBitsPerSample,(unsigned)wf->nChannels); Out(b);
            } else Out("  (WaveFormatGet failed)");
            ia->lpVtbl->Release(ia);
        }
        if(c){
            static WCHAR word[]=L"a"; SDATA d; HRESULT thr;
            d.pData=(PVOID)word; d.dwSize=sizeof(word);
            thr=c->lpVtbl->TextData(c,CHARSET_TEXT,TTSDATAFLAG_TAGGED,d,NULL,GUID_ITTSBufNotifySink);
            wsprintfA(b,"  TextData hr=0x%08lx",(unsigned long)thr); Out(b);
            Sleep(300);
            c->lpVtbl->AudioReset(c);
            c->lpVtbl->Release(c);
        }
        if(audio) audio->lpVtbl->Release(audio);
        Out("\n");
    }
    en->lpVtbl->Release(en);
    ExitProcess(0);
}
