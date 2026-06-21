/* test_s4speed.c - confirm the SAPI 4 save-speed fix.
 * Renders ~2460 chars of text to a .wav with a SAPI 4 voice (prefers Microsoft
 * Sam) twice: once with IAudioFile::RealTimeSet(0x0800) (the old value that
 * paces to real time) and once with RealTimeSet(0) (no pacing).  Prints both
 * render times.  If the fix is right, the 0 case is dramatically faster. */
#include <windows.h>
#include "speech.h"

int _fltused = 0x9875;
#pragma function(memset, memcpy)
void *__cdecl memset(void *d,int v,size_t n){ unsigned char*p=(unsigned char*)d; while(n--)*p++=(unsigned char)v; return d; }
void *__cdecl memcpy(void *d,const void *s,size_t n){ unsigned char*a=(unsigned char*)d; const unsigned char*b=(const unsigned char*)s; while(n--)*a++=*b++; return d; }
int __cdecl memcmp(const void *a,const void *b,size_t n){ const unsigned char*x=(const unsigned char*)a,*y=(const unsigned char*)b; while(n--){ if(*x!=*y) return *x-*y; x++; y++; } return 0; }

static const GUID GUID_TTSEnumerator =
    {0xd67c0280,0xc743,0x11cd,{0x80,0xe5,0x00,0xaa,0x00,0x3e,0x4b,0x50}};
static const GUID GUID_ITTSEnumW =
    {0x6b837b20,0x4a47,0x101b,{0x93,0x1a,0x00,0xaa,0x00,0x47,0xba,0x4f}};
static const GUID GUID_ITTSAttributesW =
    {0x1287a280,0x4a47,0x101b,{0x93,0x1a,0x00,0xaa,0x00,0x47,0xba,0x4f}};
static const GUID GUID_ITTSBufNotifySink =
    {0xe4963d40,0xc743,0x11cd,{0x80,0xe5,0x00,0xaa,0x00,0x3e,0x4b,0x50}};
static const GUID GUID_AudioDestFile =
    {0xd4623720,0xe4b9,0x11cf,{0x8d,0x56,0x00,0xa0,0xc9,0x03,0x4a,0x7e}};
static const GUID GUID_IAudioFile =
    {0xfd7c2320,0x3d6d,0x11b9,{0xc0,0x00,0xfe,0xd6,0xcb,0xa3,0xb1,0xa9}};
static const GUID GUID_ITTSNotifySinkW =
    {0xc0fa8f40,0x4a46,0x101b,{0x93,0x1a,0x00,0xaa,0x00,0x47,0xba,0x4f}};
static const GUID GUID_IUnknownLocal =
    {0x00000000,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }
static void OutMs(const char *l, DWORD ms){ char b[96]; wsprintfA(b,"%s%lu ms\n",l,(unsigned long)ms); Out(b); }

static volatile LONG g_audioStopped;
static HRESULT STDMETHODCALLTYPE QI(ITTSNotifySinkW *T, REFIID r, void **p){
    if(!p) return E_POINTER;
    if(IsEqualIID(r,&GUID_IUnknownLocal)||IsEqualIID(r,&GUID_ITTSNotifySinkW)){ *p=T; return S_OK; }
    *p=NULL; return E_NOINTERFACE; }
static ULONG STDMETHODCALLTYPE AR(ITTSNotifySinkW *T){ (void)T; return 1; }
static ULONG STDMETHODCALLTYPE RL(ITTSNotifySinkW *T){ (void)T; return 1; }
static HRESULT STDMETHODCALLTYPE AC(ITTSNotifySinkW *T,DWORD d){ (void)T;(void)d; return S_OK; }
static HRESULT STDMETHODCALLTYPE AStart(ITTSNotifySinkW *T,QWORD q){ (void)T;(void)q; return S_OK; }
static HRESULT STDMETHODCALLTYPE AStop(ITTSNotifySinkW *T,QWORD q){ (void)T;(void)q; InterlockedExchange(&g_audioStopped,1); return S_OK; }
static HRESULT STDMETHODCALLTYPE Vis(ITTSNotifySinkW *T,QWORD q,WCHAR a,WCHAR b,DWORD d,PTTSMOUTH m){ (void)T;(void)q;(void)a;(void)b;(void)d;(void)m; return S_OK; }
static struct ITTSNotifySinkWVtbl g_vt = { QI,AR,RL,AC,AStart,AStop,Vis };
static ITTSNotifySinkW g_sink = { &g_vt };

static WCHAR g_text[3000];

/* Render g_text to file with the given RealTime pacing value; return wall ms. */
static DWORD RenderWith(ITTSEnumW *en, GUID mode, WORD rtVal, const WCHAR *path)
{
    IAudioFile *pIAF=NULL; ITTSCentralW *fc=NULL; ITTSAttributesW *at=NULL;
    DWORD regKey=0, start, elapsed=0; SDATA d; MSG msg;
    if (FAILED(CoCreateInstance(&GUID_AudioDestFile,NULL,CLSCTX_ALL,&GUID_IAudioFile,(void**)&pIAF))||!pIAF) return 0;
    if (FAILED(en->lpVtbl->Select(en,mode,&fc,(LPUNKNOWN)pIAF))||!fc){ pIAF->lpVtbl->Release(pIAF); return 0; }
    if (SUCCEEDED(fc->lpVtbl->QueryInterface(fc,&GUID_ITTSAttributesW,(void**)&at))&&at)
        at->lpVtbl->VolumeSet(at,0xFFFFFFFF);
    pIAF->lpVtbl->RealTimeSet(pIAF, rtVal);
    g_audioStopped = 0;
    fc->lpVtbl->Register(fc,(PVOID)&g_sink,GUID_ITTSNotifySinkW,&regKey);
    pIAF->lpVtbl->Set(pIAF,(LPCWSTR)path,1);
    d.pData=(PVOID)g_text; d.dwSize=(DWORD)(lstrlenW(g_text)*sizeof(WCHAR));
    start=GetTickCount();
    fc->lpVtbl->TextData(fc,CHARSET_TEXT,TTSDATAFLAG_TAGGED,d,NULL,GUID_ITTSBufNotifySink);
    while (!g_audioStopped) {
        while (PeekMessageA(&msg,NULL,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        elapsed = GetTickCount()-start;
        if (elapsed > 90000) break;     /* safety */
        Sleep(5);
    }
    elapsed = GetTickCount()-start;
    pIAF->lpVtbl->Flush(pIAF);
    {   HANDLE h=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
        char b[64]; DWORD sz=0; if(h!=INVALID_HANDLE_VALUE){ sz=GetFileSize(h,NULL); CloseHandle(h);}
        wsprintfA(b,"    wav bytes=%lu  ",(unsigned long)sz); Out(b); }
    if (regKey) fc->lpVtbl->UnRegister(fc,regKey);
    if (at) at->lpVtbl->Release(at);
    fc->lpVtbl->Release(fc);
    pIAF->lpVtbl->Release(pIAF);
    return elapsed;
}

void __cdecl WinMainCRTStartup(void)
{
    ITTSEnumW *en=NULL; TTSMODEINFOW mi; ULONG fetched=0;
    GUID chosen; int haveMode=0; char nm[128]; int i,p=0,k;
    const WCHAR *unit=L"This is Seediffusion, a brand by a blind software developer building accessible tools. ";
    int ulen=lstrlenW(unit);

    ZeroMemory(&chosen,sizeof(chosen));
    while (p+ulen<2460){ for(k=0;unit[k];k++) g_text[p+k]=unit[k]; p+=ulen; }
    g_text[p]=0;

    CoInitialize(NULL);
    if (FAILED(CoCreateInstance(&GUID_TTSEnumerator,NULL,CLSCTX_ALL,&GUID_ITTSEnumW,(void**)&en))||!en){
        Out("SAPI 4 enumerator not available on this machine.\n"); ExitProcess(2);
    }
    en->lpVtbl->Reset(en);
    nm[0]=0;
    for (;;){
        if (en->lpVtbl->Next(en,1,&mi,&fetched)!=S_OK || fetched!=1) break;
        {   const WCHAR *src = mi.szModeName[0]?mi.szModeName:mi.szProductName;
            int isSam=0,j;
            for (j=0; src[j] && j<60; j++){
                if ((src[j]=='S'||src[j]=='s')&&(src[j+1]=='a'||src[j+1]=='A')&&(src[j+2]=='m'||src[j+2]=='M')) isSam=1;
            }
            if (!haveMode || isSam){
                chosen=mi.gModeID; haveMode=1;
                for (j=0; src[j] && j<127; j++) nm[j]=(char)src[j]; nm[j]=0;
            }
            if (isSam) break;
        }
    }
    if (!haveMode){ Out("no SAPI 4 voices found\n"); ExitProcess(3); }
    Out("voice: "); Out(nm); Out("\n");
    { char b[64]; wsprintfA(b,"text chars: %d\n",p); Out(b); }

    OutMs("RealTimeSet(0x0800)=2048 = ", RenderWith(en,chosen,0x0800,L"C:\\git\\Speakalive\\build\\s4a.wav"));
    OutMs("RealTimeSet(0x4000)       = ", RenderWith(en,chosen,0x4000,L"C:\\git\\Speakalive\\build\\s4a.wav"));
    OutMs("RealTimeSet(0xFFFF)=max   = ", RenderWith(en,chosen,0xFFFF,L"C:\\git\\Speakalive\\build\\s4a.wav"));

    DeleteFileW(L"C:\\git\\Speakalive\\build\\s4a.wav");
    en->lpVtbl->Release(en);
    CoUninitialize();
    ExitProcess(0);
}
