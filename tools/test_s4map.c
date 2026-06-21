/* test_s4map.c - verify the new slider mapping.  Replicates the app's range
 * probe (S4_ProbeHi/Lo) and S4_MapParams, then prints, per voice, the probed
 * min/def/max and the speed/pitch produced at sliders -10, 0, +10.  Success =
 * +10 lands exactly on the engine max and -10 on the min for every voice. */
#include <windows.h>
#include "speech.h"

int _fltused = 0x9875;
#pragma function(memset, memcpy)
void *__cdecl memset(void *d,int v,size_t n){ unsigned char*p=(unsigned char*)d; while(n--)*p++=(unsigned char)v; return d; }
void *__cdecl memcpy(void *d,const void *s,size_t n){ unsigned char*a=(unsigned char*)d; const unsigned char*b=(const unsigned char*)s; while(n--)*a++=*b++; return d; }
int __cdecl memcmp(const void *a,const void *b,size_t n){ const unsigned char*x=(const unsigned char*)a,*y=(const unsigned char*)b; while(n--){ if(*x!=*y) return *x-*y; x++; y++; } return 0; }

static const GUID GUID_TTSEnumerator={0xd67c0280,0xc743,0x11cd,{0x80,0xe5,0x00,0xaa,0x00,0x3e,0x4b,0x50}};
static const GUID GUID_ITTSEnumW={0x6b837b20,0x4a47,0x101b,{0x93,0x1a,0x00,0xaa,0x00,0x47,0xba,0x4f}};
static const GUID GUID_ITTSAttributesW={0x1287a280,0x4a47,0x101b,{0x93,0x1a,0x00,0xaa,0x00,0x47,0xba,0x4f}};
static const GUID GUID_AudioDestFile={0xd4623720,0xe4b9,0x11cf,{0x8d,0x56,0x00,0xa0,0xc9,0x03,0x4a,0x7e}};
static const GUID GUID_IAudioFile={0xfd7c2320,0x3d6d,0x11b9,{0xc0,0x00,0xfe,0xd6,0xcb,0xa3,0xb1,0xa9}};

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }
static TTSMODEINFOW g_mi;

/* ---- copies of the app's probe + map ---- */
static BOOL TrySet(ITTSAttributesW *a,int kind,DWORD v){ return kind==0?SUCCEEDED(a->lpVtbl->SpeedSet(a,v)):SUCCEEDED(a->lpVtbl->PitchSet(a,(WORD)v)); }
static DWORD ProbeHi(ITTSAttributesW *a,int kind,DWORD def,DWORD cap){ DWORD lo=def,hi=cap; while(lo<hi){ DWORD m=lo+(hi-lo+1)/2; if(TrySet(a,kind,m))lo=m; else hi=m-1; } return lo; }
static DWORD ProbeLo(ITTSAttributesW *a,int kind,DWORD def){ DWORD lo=1,hi=def; while(lo<hi){ DWORD m=lo+(hi-lo)/2; if(TrySet(a,kind,m))hi=m; else lo=m+1; } return hi; }

static DWORD g_sMin,g_sMax,g_pMin,g_pMax,g_dS; static WORD g_dP;
static DWORD mapSpeed(int rate){ long v; DWORD dS=g_dS?g_dS:150;
    if(g_sMin){ if(rate>=0) v=g_sMax>dS?(long)dS+(long)(g_sMax-dS)*rate/10:(long)dS;
                else        v=g_sMin<dS?(long)dS-(long)(dS-g_sMin)*(-rate)/10:(long)dS; }
    else { long st=(long)dS/10; if(st<1)st=1; v=(long)dS+(long)rate*st; } if(v<1)v=1; return (DWORD)v; }
static DWORD mapPitch(int pit){ long v; DWORD dP=g_dP?g_dP:100;
    if(g_pMin){ if(pit>=0) v=g_pMax>dP?(long)dP+(long)(g_pMax-dP)*pit/10:(long)dP;
                else       v=g_pMin<dP?(long)dP-(long)(dP-g_pMin)*(-pit)/10:(long)dP; }
    else v=(long)dP+(long)pit*5; if(v<1)v=1; if(v>0xFFFF)v=0xFFFF; return (DWORD)v; }

void __cdecl WinMainCRTStartup(void){
    ITTSEnumW *en=NULL; ULONG fetched=0; char b[256]; int n=0;
    CoInitialize(NULL);
    if(FAILED(CoCreateInstance(&GUID_TTSEnumerator,NULL,CLSCTX_ALL,&GUID_ITTSEnumW,(void**)&en))||!en){ Out("no SAPI4\n"); ExitProcess(2); }
    en->lpVtbl->Reset(en);
    Out("voice : speed[min/def/max] -> @-10/@0/@+10 | pitch[min/def/max] -> @-10/@0/@+10\n\n");
    while(en->lpVtbl->Next(en,1,&g_mi,&fetched)==S_OK && fetched==1 && n<16){
        IAudioFile *pIAF=NULL; ITTSCentralW *fc=NULL; ITTSAttributesW *at=NULL;
        const WCHAR*s=g_mi.szModeName[0]?g_mi.szModeName:g_mi.szProductName; char nm[64]; int x;
        for(x=0;s[x]&&x<60;x++)nm[x]=(char)s[x]; nm[x]=0;
        if(FAILED(CoCreateInstance(&GUID_AudioDestFile,NULL,CLSCTX_ALL,&GUID_IAudioFile,(void**)&pIAF))||!pIAF) continue;
        if(SUCCEEDED(en->lpVtbl->Select(en,g_mi.gModeID,&fc,(LPUNKNOWN)pIAF))&&fc){
            if(SUCCEEDED(fc->lpVtbl->QueryInterface(fc,&GUID_ITTSAttributesW,(void**)&at))&&at){
                at->lpVtbl->SpeedGet(at,&g_dS); at->lpVtbl->PitchGet(at,&g_dP);
                g_sMax=ProbeHi(at,0,g_dS,2000); g_sMin=ProbeLo(at,0,g_dS);
                g_pMax=ProbeHi(at,1,g_dP,0xFFFF); g_pMin=ProbeLo(at,1,g_dP);
                wsprintfA(b,"%-46s spd[%lu/%lu/%lu]->%lu/%lu/%lu  pit[%lu/%u/%lu]->%lu/%lu/%lu\n",nm,
                    (unsigned long)g_sMin,(unsigned long)g_dS,(unsigned long)g_sMax,
                    (unsigned long)mapSpeed(-10),(unsigned long)mapSpeed(0),(unsigned long)mapSpeed(10),
                    (unsigned long)g_pMin,(unsigned)g_dP,(unsigned long)g_pMax,
                    (unsigned long)mapPitch(-10),(unsigned long)mapPitch(0),(unsigned long)mapPitch(10));
                Out(b); n++;
                at->lpVtbl->Release(at);
            }
            fc->lpVtbl->Release(fc);
        }
        pIAF->lpVtbl->Release(pIAF);
    }
    en->lpVtbl->Release(en);
    ExitProcess(0);
}
