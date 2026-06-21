/* test_s4par.c - does SAPI 4 (Microsoft Sam) tolerate concurrent rendering?
 * Each worker independently enumerates, selects Sam, and renders the full text
 * to its own .wav at RealTimeSet(0xFFFF).  Compares 1 worker vs 2 workers
 * running at once.  If 2-at-once ~= 1 alone, the engine parallelises and we can
 * split a save across cores; if ~2x (or it errors), it serialises. */
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

/* Per-instance sink: first member is the COM object so This casts back to us. */
typedef struct { ITTSNotifySinkW sink; volatile LONG stopped; } MySink;

static HRESULT STDMETHODCALLTYPE QI(ITTSNotifySinkW *T, REFIID r, void **p){
    if(!p) return E_POINTER;
    if(IsEqualIID(r,&GUID_IUnknownLocal)||IsEqualIID(r,&GUID_ITTSNotifySinkW)){ *p=T; return S_OK; }
    *p=NULL; return E_NOINTERFACE; }
static ULONG STDMETHODCALLTYPE AR(ITTSNotifySinkW *T){ (void)T; return 1; }
static ULONG STDMETHODCALLTYPE RL(ITTSNotifySinkW *T){ (void)T; return 1; }
static HRESULT STDMETHODCALLTYPE AC(ITTSNotifySinkW *T,DWORD d){ (void)T;(void)d; return S_OK; }
static HRESULT STDMETHODCALLTYPE AStart(ITTSNotifySinkW *T,QWORD q){ (void)T;(void)q; return S_OK; }
static HRESULT STDMETHODCALLTYPE AStop(ITTSNotifySinkW *T,QWORD q){ (void)q; InterlockedExchange(&((MySink*)T)->stopped,1); return S_OK; }
static HRESULT STDMETHODCALLTYPE Vis(ITTSNotifySinkW *T,QWORD q,WCHAR a,WCHAR b,DWORD d,PTTSMOUTH m){ (void)T;(void)q;(void)a;(void)b;(void)d;(void)m; return S_OK; }
static struct ITTSNotifySinkWVtbl g_vt = { QI,AR,RL,AC,AStart,AStop,Vis };

static WCHAR g_text[3000];

typedef struct { const WCHAR *path; DWORD ms; } Job;

/* Full independent render of g_text -> path.  Returns wall ms via job->ms. */
static DWORD WINAPI Worker(LPVOID arg)
{
    Job *job=(Job*)arg;
    ITTSEnumW *en=NULL; TTSMODEINFOW mi; ULONG fetched=0;
    GUID mode; int haveMode=0;
    IAudioFile *pIAF=NULL; ITTSCentralW *fc=NULL; DWORD regKey=0, start; SDATA d; MSG msg;
    MySink ms; ms.sink.lpVtbl=&g_vt; ms.stopped=0;
    ZeroMemory(&mode,sizeof(mode));

    CoInitialize(NULL);
    if (FAILED(CoCreateInstance(&GUID_TTSEnumerator,NULL,CLSCTX_ALL,&GUID_ITTSEnumW,(void**)&en))||!en){ job->ms=0; CoUninitialize(); return 0; }
    en->lpVtbl->Reset(en);
    for(;;){
        if (en->lpVtbl->Next(en,1,&mi,&fetched)!=S_OK||fetched!=1) break;
        { const WCHAR *src=mi.szModeName[0]?mi.szModeName:mi.szProductName; int isSam=0,j;
          for(j=0;src[j]&&j<60;j++) if((src[j]=='S'||src[j]=='s')&&(src[j+1]=='a'||src[j+1]=='A')&&(src[j+2]=='m'||src[j+2]=='M')) isSam=1;
          if(!haveMode||isSam){ mode=mi.gModeID; haveMode=1; } if(isSam) break; }
    }
    if(!haveMode){ en->lpVtbl->Release(en); job->ms=0; CoUninitialize(); return 0; }

    if (FAILED(CoCreateInstance(&GUID_AudioDestFile,NULL,CLSCTX_ALL,&GUID_IAudioFile,(void**)&pIAF))||!pIAF) goto done;
    if (FAILED(en->lpVtbl->Select(en,mode,&fc,(LPUNKNOWN)pIAF))||!fc) goto done;
    pIAF->lpVtbl->RealTimeSet(pIAF,0xFFFF);
    fc->lpVtbl->Register(fc,(PVOID)&ms.sink,GUID_ITTSNotifySinkW,&regKey);
    pIAF->lpVtbl->Set(pIAF,(LPCWSTR)job->path,1);
    d.pData=(PVOID)g_text; d.dwSize=(DWORD)(lstrlenW(g_text)*sizeof(WCHAR));
    start=GetTickCount();
    fc->lpVtbl->TextData(fc,CHARSET_TEXT,TTSDATAFLAG_TAGGED,d,NULL,GUID_ITTSBufNotifySink);
    while(!ms.stopped){
        while(PeekMessageA(&msg,NULL,0,0,PM_REMOVE)){ TranslateMessage(&msg); DispatchMessageA(&msg); }
        if(GetTickCount()-start>90000) break;
        Sleep(5);
    }
    job->ms=GetTickCount()-start;
    pIAF->lpVtbl->Flush(pIAF);
done:
    if(regKey&&fc) fc->lpVtbl->UnRegister(fc,regKey);
    if(fc) fc->lpVtbl->Release(fc);
    if(pIAF) pIAF->lpVtbl->Release(pIAF);
    en->lpVtbl->Release(en);
    CoUninitialize();
    return 0;
}

void __cdecl WinMainCRTStartup(void)
{
    const WCHAR *unit=L"This is Seediffusion, a brand by a blind software developer building accessible tools. ";
    int ulen=lstrlenW(unit),p=0,k; DWORD t; HANDLE h0,h1;
    Job j0={L"C:\\git\\Speakalive\\build\\p0.wav",0};
    Job j1={L"C:\\git\\Speakalive\\build\\p1.wav",0};

    while(p+ulen<2460){ for(k=0;unit[k];k++) g_text[p+k]=unit[k]; p+=ulen; }
    g_text[p]=0;
    { char b[64]; wsprintfA(b,"text chars: %d\n",p); Out(b); }

    /* one render alone */
    t=GetTickCount(); h0=CreateThread(NULL,0,Worker,&j0,0,NULL); WaitForSingleObject(h0,INFINITE); CloseHandle(h0);
    OutMs("1 worker alone            = ", GetTickCount()-t);

    /* two renders at once */
    t=GetTickCount();
    h0=CreateThread(NULL,0,Worker,&j0,0,NULL);
    h1=CreateThread(NULL,0,Worker,&j1,0,NULL);
    WaitForSingleObject(h0,INFINITE); WaitForSingleObject(h1,INFINITE);
    CloseHandle(h0); CloseHandle(h1);
    OutMs("2 workers concurrently     = ", GetTickCount()-t);
    { char b[120]; wsprintfA(b,"  (worker0=%lums worker1=%lums)\n",(unsigned long)j0.ms,(unsigned long)j1.ms); Out(b); }
    Out("if 2-concurrently ~= 1-alone, SAPI 4 Sam parallelises across cores\n");

    DeleteFileW(j0.path); DeleteFileW(j1.path);
    ExitProcess(0);
}
