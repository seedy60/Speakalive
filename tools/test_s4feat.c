/* test_s4feat.c - dump each SAPI 4 voice's dwFeatures (SPEED/PITCH/VOLUME bits).
 * Confirms the feature-gated slider code won't disable sliders on voices that
 * really do support them, and flags any voice that does NOT advertise speed or
 * pitch (the kind that crashed when we called Set on it). */
#include <windows.h>
#include "speech.h"

int _fltused = 0x9875;
#pragma function(memset, memcpy)
void *__cdecl memset(void *d,int v,size_t n){ unsigned char*p=(unsigned char*)d; while(n--)*p++=(unsigned char)v; return d; }
void *__cdecl memcpy(void *d,const void *s,size_t n){ unsigned char*a=(unsigned char*)d; const unsigned char*b=(const unsigned char*)s; while(n--)*a++=*b++; return d; }
int __cdecl memcmp(const void *a,const void *b,size_t n){ const unsigned char*x=(const unsigned char*)a,*y=(const unsigned char*)b; while(n--){ if(*x!=*y) return *x-*y; x++; y++; } return 0; }

static const GUID GUID_TTSEnumerator={0xd67c0280,0xc743,0x11cd,{0x80,0xe5,0x00,0xaa,0x00,0x3e,0x4b,0x50}};
static const GUID GUID_ITTSEnumW={0x6b837b20,0x4a47,0x101b,{0x93,0x1a,0x00,0xaa,0x00,0x47,0xba,0x4f}};

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }
static TTSMODEINFOW g_mi;

void __cdecl WinMainCRTStartup(void){
    ITTSEnumW *en=NULL; ULONG fetched=0; char b[200]; int n=0,missing=0;
    CoInitialize(NULL);
    if(FAILED(CoCreateInstance(&GUID_TTSEnumerator,NULL,CLSCTX_ALL,&GUID_ITTSEnumW,(void**)&en))||!en){ Out("no SAPI4\n"); ExitProcess(2); }
    en->lpVtbl->Reset(en);
    while(en->lpVtbl->Next(en,1,&g_mi,&fetched)==S_OK && fetched==1){
        const WCHAR*s=g_mi.szModeName[0]?g_mi.szModeName:g_mi.szProductName; char nm[80]; int x;
        DWORD f=g_mi.dwFeatures;
        int sp=(f&TTSFEATURE_SPEED)!=0, pi=(f&TTSFEATURE_PITCH)!=0, vo=(f&TTSFEATURE_VOLUME)!=0;
        for(x=0;s[x]&&x<79;x++)nm[x]=(char)s[x]; nm[x]=0;
        wsprintfA(b,"%-50s feat=0x%08lx  speed=%d pitch=%d volume=%d%s\n",nm,(unsigned long)f,sp,pi,vo,
            (sp&&pi)?"":"   <-- missing speed/pitch");
        Out(b); n++; if(!(sp&&pi)) missing++;
    }
    wsprintfA(b,"\n%d voices; %d missing speed and/or pitch support.\n",n,missing); Out(b);
    en->lpVtbl->Release(en);
    ExitProcess(0);
}
