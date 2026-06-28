/* test_s4hide.c - confirm the A-law/mu-law hide filter (S4_IsTelephonyOnly)
 * catches exactly the telephony "...<rate> A/U" voices and nothing else. */
#include <windows.h>
#include "speech.h"

int _fltused = 0x9875;
#pragma function(memset, memcpy)
void *__cdecl memset(void *d,int v,size_t n){ unsigned char*p=(unsigned char*)d; while(n--)*p++=(unsigned char)v; return d; }
void *__cdecl memcpy(void *d,const void *s,size_t n){ unsigned char*a=(unsigned char*)d; const unsigned char*b=(const unsigned char*)s; while(n--)*a++=*b++; return d; }
int __cdecl memcmp(const void *a,const void *b,size_t n){ const unsigned char*x=(const unsigned char*)a,*y=(const unsigned char*)b; while(n--){ if(*x!=*y)return *x-*y; x++; y++; } return 0; }

static const GUID GUID_TTSEnumerator={0xd67c0280,0xc743,0x11cd,{0x80,0xe5,0x00,0xaa,0x00,0x3e,0x4b,0x50}};
static const GUID GUID_ITTSEnumW={0x6b837b20,0x4a47,0x101b,{0x93,0x1a,0x00,0xaa,0x00,0x47,0xba,0x4f}};
static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }
static TTSMODEINFOW g_mi;

/* exact copy of the app's filter */
static int telephonyOnly(const char *name){ int n=lstrlenA(name);
    return n>=3 && name[n-2]==' ' && (name[n-1]=='A'||name[n-1]=='U') && name[n-3]>='0' && name[n-3]<='9'; }

void __cdecl WinMainCRTStartup(void){
    ITTSEnumW *en=NULL; ULONG fetched=0; char b[160]; int hidden=0,kept=0;
    CoInitialize(NULL);
    if(FAILED(CoCreateInstance(&GUID_TTSEnumerator,NULL,CLSCTX_ALL,&GUID_ITTSEnumW,(void**)&en))||!en){ Out("no SAPI4\n"); ExitProcess(2); }
    en->lpVtbl->Reset(en);
    while(en->lpVtbl->Next(en,1,&g_mi,&fetched)==S_OK && fetched==1){
        const WCHAR *s=g_mi.szModeName[0]?g_mi.szModeName:g_mi.szProductName; char nm[96]; int i;
        for(i=0;s[i]&&i<95;i++)nm[i]=(char)s[i]; nm[i]=0;
        if(telephonyOnly(nm)){ wsprintfA(b,"  HIDE : %s\n",nm); Out(b); hidden++; }
        else kept++;
    }
    wsprintfA(b,"\nhidden=%d  kept=%d\n",hidden,kept); Out(b);
    en->lpVtbl->Release(en);
    ExitProcess(0);
}
