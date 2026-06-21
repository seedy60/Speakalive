/* test_s4safe.c - is the audio artifact from concurrency or from chunk joins?
 * Renders each chunk of the text TWICE: once alone/serially (reference) and once
 * as part of the concurrent pool, then hashes each chunk's .wav.  If every
 * concurrent hash equals its serial hash, the engine renders byte-identically
 * under concurrency (so any artifact is purely the chunk-join, fixable by
 * smarter chunking).  Any mismatch means concurrency corrupts that voice. */
#include <windows.h>
#include "speech.h"

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

#define MAXCH 32
static WCHAR  g_text[3000];
static WCHAR *g_chunk[MAXCH];
static WCHAR  g_path[MAXCH][MAX_PATH];
static int    g_count;
static GUID   g_mode;
static volatile LONG g_next, g_fail;

static int ChunkLen(const WCHAR*w,int start,int total,int max){
    int i,lb=-1,ls=-1; if(total-start<=max) return total-start;
    for(i=0;i<max;i++){ WCHAR c=w[start+i]; if(c=='.'||c=='!'||c=='?'||c=='\n')lb=i; if(c==' '||c=='\t'||c=='\r')ls=i; }
    if(lb>=0)return lb+1; if(ls>=0)return ls+1; return max;
}
static BOOL RenderChunk(ITTSEnumW *en,const WCHAR *chunk,const WCHAR *path){
    IAudioFile *pIAF=NULL; ITTSCentralW *fc=NULL; DWORD regKey=0,start; SDATA d; MSG msg; S4Sink sink; BOOL ok=FALSE;
    sink.sink.lpVtbl=&g_vt; sink.stopped=0;
    if(FAILED(CoCreateInstance(&GUID_AudioDestFile,NULL,CLSCTX_ALL,&GUID_IAudioFile,(void**)&pIAF))||!pIAF)return FALSE;
    if(FAILED(en->lpVtbl->Select(en,g_mode,&fc,(LPUNKNOWN)pIAF))||!fc)goto done;
    pIAF->lpVtbl->RealTimeSet(pIAF,0xFFFF);
    fc->lpVtbl->Register(fc,(PVOID)&sink.sink,GUID_ITTSNotifySinkW,&regKey);
    if(FAILED(pIAF->lpVtbl->Set(pIAF,(LPCWSTR)path,1)))goto done;
    d.pData=(PVOID)chunk; d.dwSize=(DWORD)((lstrlenW(chunk)+1)*sizeof(WCHAR));
    if(FAILED(fc->lpVtbl->TextData(fc,CHARSET_TEXT,TTSDATAFLAG_TAGGED,d,NULL,GUID_ITTSBufNotifySink)))goto done;
    start=GetTickCount();
    while(!sink.stopped){ while(PeekMessageA(&msg,NULL,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageA(&msg);} if(GetTickCount()-start>90000)break; Sleep(2); }
    pIAF->lpVtbl->Flush(pIAF); ok=TRUE;
done:
    if(regKey&&fc)fc->lpVtbl->UnRegister(fc,regKey);
    if(fc)fc->lpVtbl->Release(fc); if(pIAF)pIAF->lpVtbl->Release(pIAF);
    return ok;
}
static DWORD WINAPI Worker(LPVOID a){
    ITTSEnumW *en=NULL; (void)a; CoInitialize(NULL);
    if(SUCCEEDED(CoCreateInstance(&GUID_TTSEnumerator,NULL,CLSCTX_ALL,&GUID_ITTSEnumW,(void**)&en))&&en){
        for(;;){ LONG i=InterlockedIncrement(&g_next)-1; if(i>=g_count||g_fail)break;
            if(!RenderChunk(en,g_chunk[i],g_path[i])){InterlockedExchange(&g_fail,1);break;} }
        en->lpVtbl->Release(en);
    } else InterlockedExchange(&g_fail,1);
    CoUninitialize(); return 0;
}
/* FNV-1a over a file's bytes; 0 on failure. */
static unsigned Hash(const WCHAR *path){
    HANDLE h=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    unsigned hsh=2166136261u; BYTE buf[1024]; DWORD got;
    if(h==INVALID_HANDLE_VALUE)return 0;
    while(ReadFile(h,buf,sizeof(buf),&got,NULL)&&got){ DWORD i; for(i=0;i<got;i++){ hsh^=buf[i]; hsh*=16777619u; } }
    CloseHandle(h); return hsh;
}
static void mkpath(int i,const char*tag){ char dir[MAX_PATH],f[MAX_PATH]; DWORD n=GetTempPathA(MAX_PATH,dir);
    if(!n||n>MAX_PATH)lstrcpyA(dir,".\\"); GetTempFileNameA(dir,tag,0,f); { int x; for(x=0;f[x];x++)g_path[i][x]=(WCHAR)f[x]; g_path[i][x]=0; } }

void __cdecl WinMainCRTStartup(void){
    ITTSEnumW *en=NULL; TTSMODEINFOW mi; ULONG fetched=0; int haveMode=0; char nm[128];
    const WCHAR *unit=L"This is Seediffusion, a brand by a blind software developer building accessible tools and tooling. ";
    int ulen=lstrlenW(unit),p=0,k,i,total,pos,cs,cores,T; SYSTEM_INFO si; HANDLE th[8];
    unsigned refH[MAXCH]; char b[160]; int mismatch=0;

    while(p+ulen<2460){ for(k=0;unit[k];k++)g_text[p+k]=unit[k]; p+=ulen; } g_text[p]=0; total=p;
    CoInitialize(NULL);
    if(FAILED(CoCreateInstance(&GUID_TTSEnumerator,NULL,CLSCTX_ALL,&GUID_ITTSEnumW,(void**)&en))||!en){Out("no SAPI4\n");ExitProcess(2);}
    en->lpVtbl->Reset(en); nm[0]=0;
    for(;;){ if(en->lpVtbl->Next(en,1,&mi,&fetched)!=S_OK||fetched!=1)break;
        { const WCHAR*src=mi.szModeName[0]?mi.szModeName:mi.szProductName; int isM=0,j;
          for(j=0;src[j]&&j<60;j++) if((src[j]=='M'||src[j]=='m')&&(src[j+1]=='i')&&(src[j+2]=='c')&&(src[j+3]=='h'))isM=1;
          if(!haveMode||isM){g_mode=mi.gModeID;haveMode=1;{int x;for(x=0;src[x]&&x<127;x++)nm[x]=(char)src[x];nm[x]=0;}} if(isM)break; } }
    en->lpVtbl->Release(en);
    if(!haveMode){Out("no voice\n");ExitProcess(3);}
    Out("voice: "); Out(nm); Out("\n");

    GetSystemInfo(&si); cores=(int)si.dwNumberOfProcessors; if(cores<1)cores=1; if(cores>8)cores=8;
    cs=total/cores; if(cs>4000)cs=4000; if(cs<256)cs=256;
    for(pos=0;pos<total&&g_count<MAXCH;){ int clen=ChunkLen(g_text,pos,total,cs);
        g_chunk[g_count]=(WCHAR*)HeapAlloc(GetProcessHeap(),0,(clen+1)*sizeof(WCHAR));
        memcpy(g_chunk[g_count],g_text+pos,clen*sizeof(WCHAR)); g_chunk[g_count][clen]=0;
        pos+=clen; g_count++; }
    wsprintfA(b,"chunks: %d\n",g_count); Out(b);

    /* PASS 1: render each chunk alone, serially, on this thread = reference. */
    CoInitialize(NULL);
    CoCreateInstance(&GUID_TTSEnumerator,NULL,CLSCTX_ALL,&GUID_ITTSEnumW,(void**)&en);
    for(i=0;i<g_count;i++){ mkpath(i,"ref"); RenderChunk(en,g_chunk[i],g_path[i]); refH[i]=Hash(g_path[i]); DeleteFileW(g_path[i]); }
    if(en)en->lpVtbl->Release(en);

    /* PASS 2: render all chunks at once via the concurrent pool. */
    g_next=0; g_fail=0; T=g_count<cores?g_count:cores;
    for(i=0;i<g_count;i++) mkpath(i,"cnc");
    for(i=0;i<T;i++) th[i]=CreateThread(NULL,0,Worker,NULL,0,NULL);
    for(i=0;i<T;i++){ WaitForSingleObject(th[i],INFINITE); CloseHandle(th[i]); }

    for(i=0;i<g_count;i++){ unsigned h=Hash(g_path[i]); DeleteFileW(g_path[i]);
        if(h!=refH[i]){ mismatch++; wsprintfA(b,"  chunk %d: MISMATCH serial=%08x concurrent=%08x\n",i,refH[i],h); Out(b); } }
    if(!mismatch) Out("RESULT: every chunk byte-identical serial vs concurrent -> concurrency is SAFE.\n");
    else          Out("RESULT: concurrency CHANGES the audio -> must serialise this voice.\n");
    ExitProcess(0);
}
