/* test_s4sweep.c - automatically screen EVERY installed SAPI 4 voice for the
 * only artifact serial rendering can introduce: a discontinuity where two
 * chunks are stitched (a "click").  For each voice it renders a multi-sentence
 * passage in chunks exactly as S4_Save does (serial, broken at sentence ends),
 * concatenates the PCM, and measures the sample jump across each join versus the
 * sharpest natural jump elsewhere.  A join jump that is small (or no bigger than
 * the natural signal) means no audible click.  No listening required. */
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

#define MAXV 80
#define ACCMAX (24*1024*1024)
static GUID  g_mode[MAXV];
static char  g_name[MAXV][96];
static int   g_nv;
static WCHAR g_text[2000];
static BYTE *g_acc;                 /* accumulated PCM for the current voice */
static DWORD g_accLen;
static DWORD g_join[16];            /* byte offsets of chunk joins           */
static int   g_nj;
static TTSMODEINFOW g_mi;           /* big - kept off the stack             */
static WCHAR g_cb[400];
static WCHAR g_pathbuf[MAX_PATH];

static int ChunkLen(const WCHAR*w,int start,int total,int max){
    int i,lb=-1,ls=-1; if(total-start<=max) return total-start;
    for(i=0;i<max;i++){ WCHAR c=w[start+i]; if(c=='.'||c=='!'||c=='?'||c=='\n')lb=i; if(c==' '||c=='\t'||c=='\r')ls=i; }
    if(lb>=0)return lb+1; if(ls>=0)return ls+1; return max;
}
/* Render one chunk with 'mode' to 'path'.  FALSE on failure. */
static BOOL RenderChunk(ITTSEnumW *en, GUID mode, const WCHAR *chunk, const WCHAR *path){
    IAudioFile *pIAF=NULL; ITTSCentralW *fc=NULL; DWORD regKey=0,start; SDATA d; MSG msg; S4Sink sink; BOOL ok=FALSE;
    sink.sink.lpVtbl=&g_vt; sink.stopped=0;
    if(FAILED(CoCreateInstance(&GUID_AudioDestFile,NULL,CLSCTX_ALL,&GUID_IAudioFile,(void**)&pIAF))||!pIAF)return FALSE;
    if(FAILED(en->lpVtbl->Select(en,mode,&fc,(LPUNKNOWN)pIAF))||!fc)goto done;
    pIAF->lpVtbl->RealTimeSet(pIAF,0xFFFF);
    fc->lpVtbl->Register(fc,(PVOID)&sink.sink,GUID_ITTSNotifySinkW,&regKey);
    if(FAILED(pIAF->lpVtbl->Set(pIAF,(LPCWSTR)path,1)))goto done;
    d.pData=(PVOID)chunk; d.dwSize=(DWORD)((lstrlenW(chunk)+1)*sizeof(WCHAR));
    if(FAILED(fc->lpVtbl->TextData(fc,CHARSET_TEXT,TTSDATAFLAG_TAGGED,d,NULL,GUID_ITTSBufNotifySink)))goto done;
    start=GetTickCount();
    while(!sink.stopped){ while(PeekMessageA(&msg,NULL,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageA(&msg);} if(GetTickCount()-start>120000)break; Sleep(2); }
    pIAF->lpVtbl->Flush(pIAF); ok=TRUE;
done:
    if(regKey&&fc)fc->lpVtbl->UnRegister(fc,regKey);
    if(fc)fc->lpVtbl->Release(fc); if(pIAF)pIAF->lpVtbl->Release(pIAF);
    return ok;
}
/* Append the PCM (data chunk) of WAV 'path' to g_acc.  FALSE on parse fail. */
static BOOL AppendPcm(const WCHAR *path){
    HANDLE h=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    DWORD sz,got,pos; BYTE *buf; BOOL ok=FALSE;
    if(h==INVALID_HANDLE_VALUE)return FALSE;
    sz=GetFileSize(h,NULL);
    if(sz==INVALID_FILE_SIZE||sz<44){ CloseHandle(h); return FALSE; }
    buf=(BYTE*)HeapAlloc(GetProcessHeap(),0,sz);
    if(!buf){ CloseHandle(h); return FALSE; }
    if(!ReadFile(h,buf,sz,&got,NULL)||got!=sz){ CloseHandle(h); HeapFree(GetProcessHeap(),0,buf); return FALSE; }
    CloseHandle(h);
    pos=12;
    while(pos+8<=sz){ DWORD id=*(DWORD*)(buf+pos),len=*(DWORD*)(buf+pos+4);
        if(pos+8+len>sz)len=sz-pos-8;
        if(id==0x61746164){ if(g_accLen+len<=ACCMAX){ memcpy(g_acc+g_accLen,buf+pos+8,len); g_accLen+=len; ok=TRUE; } break; }
        pos+=8+len+(len&1); }
    HeapFree(GetProcessHeap(),0,buf);
    return ok;
}
static void mkpath(WCHAR *out){ char dir[MAX_PATH],f[MAX_PATH]; DWORD n=GetTempPathA(MAX_PATH,dir);
    if(!n||n>MAX_PATH)lstrcpyA(dir,".\\"); GetTempFileNameA(dir,"swp",0,f); { int x; for(x=0;f[x];x++)out[x]=(WCHAR)f[x]; out[x]=0; } }

void __cdecl WinMainCRTStartup(void){
    ITTSEnumW *en=NULL; ULONG fetched=0; int i,k,p,total,pos,flagged=0,errors=0,clean=0;
    const WCHAR *unit=L"This is a brand by a blind developer. It builds accessible tools for everyone. ";
    int ulen; char b[256];

    g_acc=(BYTE*)HeapAlloc(GetProcessHeap(),0,ACCMAX);
    ulen=lstrlenW(unit); p=0;
    while(p+ulen<900){ for(k=0;unit[k];k++)g_text[p+k]=unit[k]; p+=ulen; } g_text[p]=0; total=p;

    CoInitialize(NULL);
    if(FAILED(CoCreateInstance(&GUID_TTSEnumerator,NULL,CLSCTX_ALL,&GUID_ITTSEnumW,(void**)&en))||!en){ Out("no SAPI4\n"); ExitProcess(2); }
    en->lpVtbl->Reset(en);
    while(g_nv<MAXV){ if(en->lpVtbl->Next(en,1,&g_mi,&fetched)!=S_OK||fetched!=1)break;
        { const WCHAR*s=g_mi.szModeName[0]?g_mi.szModeName:g_mi.szProductName; int x; g_mode[g_nv]=g_mi.gModeID;
          for(x=0;s[x]&&x<95;x++)g_name[g_nv][x]=(char)s[x]; g_name[g_nv][x]=0; g_nv++; } }
    wsprintfA(b,"Found %d SAPI 4 voices.  Screening chunk joins (chunk size 300, ~%d chars each):\n\n",g_nv,total); Out(b);

    for(i=0;i<g_nv;i++){
        BOOL okv=TRUE; int chunks=0;
        g_accLen=0; g_nj=0;
        for(pos=0; pos<total && okv; ){
            int clen=ChunkLen(g_text,pos,total,300);
            memcpy(g_cb,g_text+pos,clen*sizeof(WCHAR)); g_cb[clen]=0;
            mkpath(g_pathbuf);
            if(!RenderChunk(en,g_mode[i],g_cb,g_pathbuf)){ okv=FALSE; DeleteFileW(g_pathbuf); break; }
            if(chunks>0 && g_nj<16) g_join[g_nj++]=g_accLen;     /* join at current end */
            if(!AppendPcm(g_pathbuf)){ okv=FALSE; DeleteFileW(g_pathbuf); break; }
            DeleteFileW(g_pathbuf);
            chunks++; pos+=clen;
        }
        if(!okv || g_accLen<4){ wsprintfA(b,"  [ERR ] %s\n",g_name[i]); Out(b); errors++; continue; }
        {   short *s=(short*)g_acc; DWORD ns=g_accLen/2, x; int j;
            long maxNat=0, maxJoin=0; int worstAtJoin=0;
            /* sharpest natural adjacent jump, skipping join sample positions */
            for(x=1;x<ns;x++){ long dpos=(long)(x*2); int isJoin=0;
                for(j=0;j<g_nj;j++) if((long)g_join[j]==dpos){ isJoin=1; break; }
                { long dv=s[x]-s[x-1]; if(dv<0)dv=-dv; if(isJoin){ if(dv>maxJoin){maxJoin=dv;} } else { if(dv>maxNat)maxNat=dv; } }
            }
            { int v=s[0]; (void)v; }
            worstAtJoin=(maxNat>0 && maxJoin>maxNat);
            if(g_nj==0){ wsprintfA(b,"  [ OK ] %s  (single chunk - no joins)\n",g_name[i]); Out(b); clean++; }
            else if(maxJoin<1200 || !worstAtJoin){
                wsprintfA(b,"  [ OK ] %s  joins=%d  maxJoinJump=%ld (natural=%ld)\n",g_name[i],g_nj,maxJoin,maxNat); Out(b); clean++;
            } else {
                wsprintfA(b,"  [FLAG] %s  joins=%d  maxJoinJump=%ld EXCEEDS natural=%ld\n",g_name[i],g_nj,maxJoin,maxNat); Out(b); flagged++;
            }
        }
    }
    wsprintfA(b,"\nDone.  clean=%d  flagged=%d  errors=%d  of %d voices.\n",clean,flagged,errors,g_nv); Out(b);
    en->lpVtbl->Release(en);
    ExitProcess(0);
}
