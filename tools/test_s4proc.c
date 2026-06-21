/* test_s4proc.c - can SAPI 4 be parallelised across PROCESSES instead of
 * threads?  In-process concurrency corrupts the L&H engine (test_s4safe), but
 * the shared state may be per-process.  This exe re-invokes itself as child
 * renderers: it renders a fixed Michelle chunk once alone (reference), then two
 * children AT THE SAME TIME, and checks each child's .wav against the reference.
 * All match -> separate processes are isolated -> multi-process parallel save is
 * viable.  Mismatch/empty -> even processes share state, so serial is the floor. */
#include <windows.h>
#include "speech.h"
#include <shellapi.h>

int _fltused = 0x9875;
#pragma function(memset, memcpy)
void *__cdecl memset(void *d,int v,size_t n){ unsigned char*p=(unsigned char*)d; while(n--)*p++=(unsigned char)v; return d; }
void *__cdecl memcpy(void *d,const void *s,size_t n){ unsigned char*a=(unsigned char*)d; const unsigned char*b=(const unsigned char*)s; while(n--)*a++=*b++; return d; }
int __cdecl memcmp(const void *a,const void *b,size_t n){ const unsigned char*x=(const unsigned char*)a,*y=(const unsigned char*)b; while(n--){ if(*x!=*y) return *x-*y; x++; y++; } return 0; }

static const GUID GUID_TTSEnumerator={0xd67c0280,0xc743,0x11cd,{0x80,0xe5,0x00,0xaa,0x00,0x3e,0x4b,0x50}};
static const GUID GUID_ITTSEnumW={0x6b837b20,0x4a47,0x101b,{0x93,0x1a,0x00,0xaa,0x00,0x47,0xba,0x4f}};
static const GUID GUID_ITTSBufNotifySink={0xe4963d40,0xc743,0x11cd,{0x80,0xe5,0x00,0xaa,0x00,0x3e,0x4b,0x50}};
static const GUID GUID_AudioDestFile={0xd4623720,0xe4b9,0x11cf,{0x8d,0x56,0x00,0xa0,0xc9,0x03,0x4a,0x7e}};
static const GUID GUID_IAudioFile={0xfd7c2320,0x3d6d,0x11b9,{0xc0,0x00,0xfe,0xd6,0xcb,0xa3,0xb1,0xa9}};
static const GUID GUID_ITTSNotifySinkW={0xc0fa8f40,0x4a46,0x101b,{0x93,0x1a,0x00,0xaa,0x00,0x47,0xba,0x4f}};
static const GUID GUID_IUnknownLocal={0x00000000,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }

typedef struct { ITTSNotifySinkW sink; volatile LONG stopped; } S4Sink;
static HRESULT STDMETHODCALLTYPE QI(ITTSNotifySinkW *T, REFIID r, void **p){ if(!p)return E_POINTER; if(IsEqualIID(r,&GUID_IUnknownLocal)||IsEqualIID(r,&GUID_ITTSNotifySinkW)){*p=T;return S_OK;} *p=NULL; return E_NOINTERFACE; }
static ULONG STDMETHODCALLTYPE AR(ITTSNotifySinkW *T){(void)T;return 1;}
static ULONG STDMETHODCALLTYPE RL(ITTSNotifySinkW *T){(void)T;return 1;}
static HRESULT STDMETHODCALLTYPE AC(ITTSNotifySinkW *T,DWORD d){(void)T;(void)d;return S_OK;}
static HRESULT STDMETHODCALLTYPE AStart(ITTSNotifySinkW *T,QWORD q){(void)T;(void)q;return S_OK;}
static HRESULT STDMETHODCALLTYPE AStop(ITTSNotifySinkW *T,QWORD q){(void)q;InterlockedExchange(&((S4Sink*)T)->stopped,1);return S_OK;}
static HRESULT STDMETHODCALLTYPE Vis(ITTSNotifySinkW *T,QWORD q,WCHAR a,WCHAR b,DWORD d,PTTSMOUTH m){(void)T;(void)q;(void)a;(void)b;(void)d;(void)m;return S_OK;}
static struct ITTSNotifySinkWVtbl g_vt={QI,AR,RL,AC,AStart,AStop,Vis};

static WCHAR g_chunk[1200];
static void buildText(void){ const WCHAR *u=L"This is Seediffusion, a brand by a blind developer building accessible tools. ";
    int ul=lstrlenW(u),p=0,k; while(p+ul<600){ for(k=0;u[k];k++)g_chunk[p+k]=u[k]; p+=ul; } g_chunk[p]=0; }

static int findMichelle(ITTSEnumW *en, GUID *mode){ TTSMODEINFOW mi; ULONG f=0; int have=0;
    en->lpVtbl->Reset(en);
    for(;;){ if(en->lpVtbl->Next(en,1,&mi,&f)!=S_OK||f!=1)break;
        { const WCHAR*s=mi.szModeName[0]?mi.szModeName:mi.szProductName; int isM=0,j;
          for(j=0;s[j]&&j<60;j++) if((s[j]=='M'||s[j]=='m')&&s[j+1]=='i'&&s[j+2]=='c'&&s[j+3]=='h')isM=1;
          if(!have||isM){*mode=mi.gModeID;have=1;} if(isM)break; } }
    return have;
}
static BOOL RenderTo(const WCHAR *path){
    ITTSEnumW *en=NULL; GUID mode; IAudioFile *pIAF=NULL; ITTSCentralW *fc=NULL; DWORD regKey=0,start; SDATA d; MSG msg; S4Sink sink; BOOL ok=FALSE;
    if(FAILED(CoCreateInstance(&GUID_TTSEnumerator,NULL,CLSCTX_ALL,&GUID_ITTSEnumW,(void**)&en))||!en) return FALSE;
    if(!findMichelle(en,&mode)){ en->lpVtbl->Release(en); return FALSE; }
    sink.sink.lpVtbl=&g_vt; sink.stopped=0;
    if(FAILED(CoCreateInstance(&GUID_AudioDestFile,NULL,CLSCTX_ALL,&GUID_IAudioFile,(void**)&pIAF))||!pIAF) goto done;
    if(FAILED(en->lpVtbl->Select(en,mode,&fc,(LPUNKNOWN)pIAF))||!fc) goto done;
    pIAF->lpVtbl->RealTimeSet(pIAF,0xFFFF);
    fc->lpVtbl->Register(fc,(PVOID)&sink.sink,GUID_ITTSNotifySinkW,&regKey);
    if(FAILED(pIAF->lpVtbl->Set(pIAF,(LPCWSTR)path,1))) goto done;
    d.pData=(PVOID)g_chunk; d.dwSize=(DWORD)((lstrlenW(g_chunk)+1)*sizeof(WCHAR));
    if(FAILED(fc->lpVtbl->TextData(fc,CHARSET_TEXT,TTSDATAFLAG_TAGGED,d,NULL,GUID_ITTSBufNotifySink))) goto done;
    start=GetTickCount();
    while(!sink.stopped){ while(PeekMessageA(&msg,NULL,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageA(&msg);} if(GetTickCount()-start>90000)break; Sleep(2); }
    pIAF->lpVtbl->Flush(pIAF); ok=TRUE;
done:
    if(regKey&&fc)fc->lpVtbl->UnRegister(fc,regKey);
    if(fc)fc->lpVtbl->Release(fc); if(pIAF)pIAF->lpVtbl->Release(pIAF);
    en->lpVtbl->Release(en); return ok;
}
static unsigned Hash(const WCHAR *path){
    HANDLE h=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    unsigned hs=2166136261u; BYTE buf[1024]; DWORD got;
    if(h==INVALID_HANDLE_VALUE)return 0;
    while(ReadFile(h,buf,sizeof(buf),&got,NULL)&&got){ DWORD i; for(i=0;i<got;i++){hs^=buf[i];hs*=16777619u;} }
    CloseHandle(h); return hs;
}
static void mkpath(WCHAR *out,const char*tag){ char dir[MAX_PATH],f[MAX_PATH]; DWORD n=GetTempPathA(MAX_PATH,dir);
    if(!n||n>MAX_PATH)lstrcpyA(dir,".\\"); GetTempFileNameA(dir,tag,0,f); { int x; for(x=0;f[x];x++)out[x]=(WCHAR)f[x]; out[x]=0; } }

/* Spawn this exe as: "<self>" child <path>.  Returns the process handle. */
static HANDLE spawnChild(const WCHAR *self, const WCHAR *path){
    WCHAR cmd[1024]; STARTUPINFOW si; PROCESS_INFORMATION pi; int n=0,k;
    cmd[n++]='"'; for(k=0;self[k];k++)cmd[n++]=self[k]; cmd[n++]='"';
    cmd[n++]=' '; cmd[n++]='c'; cmd[n++]='h'; cmd[n++]='i'; cmd[n++]='l'; cmd[n++]='d'; cmd[n++]=' ';
    cmd[n++]='"'; for(k=0;path[k];k++)cmd[n++]=path[k]; cmd[n++]='"'; cmd[n]=0;
    ZeroMemory(&si,sizeof(si)); si.cb=sizeof(si); ZeroMemory(&pi,sizeof(pi));
    if(!CreateProcessW(NULL,cmd,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi)) return NULL;
    CloseHandle(pi.hThread); return pi.hProcess;
}

void __cdecl WinMainCRTStartup(void){
    int argc=0; LPWSTR *argv; WCHAR self[MAX_PATH]; char b[160];
    WCHAR ref[MAX_PATH], c0[MAX_PATH], c1[MAX_PATH]; unsigned hr,h0,h1; HANDLE p0,p1;

    CoInitialize(NULL);
    buildText();
    argv=CommandLineToArgvW(GetCommandLineW(),&argc);

    if(argc>=3 && argv[1][0]=='c' && argv[1][1]=='h'){   /* child mode: render+exit */
        RenderTo(argv[2]);
        CoUninitialize(); ExitProcess(0);
    }

    GetModuleFileNameW(NULL,self,MAX_PATH);

    /* reference: one child alone */
    mkpath(ref,"pr0"); p0=spawnChild(self,ref);
    if(!p0){ Out("spawn failed\n"); ExitProcess(2);} WaitForSingleObject(p0,INFINITE); CloseHandle(p0);
    hr=Hash(ref); DeleteFileW(ref);

    /* two children AT ONCE */
    mkpath(c0,"pc0"); mkpath(c1,"pc1");
    p0=spawnChild(self,c0); p1=spawnChild(self,c1);
    if(p0)WaitForSingleObject(p0,INFINITE); if(p1)WaitForSingleObject(p1,INFINITE);
    if(p0)CloseHandle(p0); if(p1)CloseHandle(p1);
    h0=Hash(c0); h1=Hash(c1); DeleteFileW(c0); DeleteFileW(c1);

    wsprintfA(b,"reference=%08x  concurrent0=%08x  concurrent1=%08x\n",hr,h0,h1); Out(b);
    if(hr && h0==hr && h1==hr) Out("RESULT: separate processes render IDENTICALLY -> multi-process parallel is VIABLE.\n");
    else                       Out("RESULT: processes still differ -> the engine shares state even across processes; serial is the floor.\n");
    ExitProcess(0);
}
