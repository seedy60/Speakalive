/* test_s4null.c - can a SAPI 4 voice be Select()ed with NO audio destination
 * (NULL) and still speak to the speakers?  If so we avoid the MMAudioDest object
 * entirely - nothing to release/reuse/leak, so the voice-select crash and the
 * resource exhaustion both disappear.  Selecting is checked silently; then it
 * speaks one short word.  Real-time elapsed => audio really played; near-instant
 * => NULL produced no sound (so NULL is not usable for live speech). */
#include <windows.h>
#include "speech.h"

int _fltused = 0x9875;
#pragma function(memset, memcpy)
void *__cdecl memset(void *d,int v,size_t n){ unsigned char*p=(unsigned char*)d; while(n--)*p++=(unsigned char)v; return d; }
void *__cdecl memcpy(void *d,const void *s,size_t n){ unsigned char*a=(unsigned char*)d; const unsigned char*b=(const unsigned char*)s; while(n--)*a++=*b++; return d; }
int __cdecl memcmp(const void *a,const void *b,size_t n){ const unsigned char*x=(const unsigned char*)a,*y=(const unsigned char*)b; while(n--){ if(*x!=*y)return *x-*y; x++; y++; } return 0; }

static const GUID GUID_TTSEnumerator={0xd67c0280,0xc743,0x11cd,{0x80,0xe5,0x00,0xaa,0x00,0x3e,0x4b,0x50}};
static const GUID GUID_ITTSEnumW={0x6b837b20,0x4a47,0x101b,{0x93,0x1a,0x00,0xaa,0x00,0x47,0xba,0x4f}};
static const GUID GUID_ITTSAttributesW={0x1287a280,0x4a47,0x101b,{0x93,0x1a,0x00,0xaa,0x00,0x47,0xba,0x4f}};
static const GUID GUID_ITTSNotifySinkW={0xc0fa8f40,0x4a46,0x101b,{0x93,0x1a,0x00,0xaa,0x00,0x47,0xba,0x4f}};
static const GUID GUID_ITTSBufNotifySink={0xe4963d40,0xc743,0x11cd,{0x80,0xe5,0x00,0xaa,0x00,0x3e,0x4b,0x50}};
static const GUID GUID_IUnknownLocal={0x00000000,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }
static TTSMODEINFOW g_mi;
static volatile LONG g_stopped;

static HRESULT STDMETHODCALLTYPE QI(ITTSNotifySinkW *T, REFIID r, void **p){ if(!p)return E_POINTER; if(IsEqualIID(r,&GUID_IUnknownLocal)||IsEqualIID(r,&GUID_ITTSNotifySinkW)){*p=T;return S_OK;} *p=NULL; return E_NOINTERFACE; }
static ULONG STDMETHODCALLTYPE AR(ITTSNotifySinkW *T){(void)T;return 1;}
static ULONG STDMETHODCALLTYPE RL(ITTSNotifySinkW *T){(void)T;return 1;}
static HRESULT STDMETHODCALLTYPE AC(ITTSNotifySinkW *T,DWORD d){(void)T;(void)d;return S_OK;}
static HRESULT STDMETHODCALLTYPE AStart(ITTSNotifySinkW *T,QWORD q){(void)T;(void)q;return S_OK;}
static HRESULT STDMETHODCALLTYPE AStop(ITTSNotifySinkW *T,QWORD q){(void)T;(void)q;InterlockedExchange(&g_stopped,1);return S_OK;}
static HRESULT STDMETHODCALLTYPE Vis(ITTSNotifySinkW *T,QWORD q,WCHAR a,WCHAR b,DWORD d,PTTSMOUTH m){(void)T;(void)q;(void)a;(void)b;(void)d;(void)m;return S_OK;}
static struct ITTSNotifySinkWVtbl g_vt={QI,AR,RL,AC,AStart,AStop,Vis};
static ITTSNotifySinkW g_sink={&g_vt};

void __cdecl WinMainCRTStartup(void){
    ITTSEnumW *en=NULL; ITTSCentralW *c=NULL; ITTSAttributesW *at=NULL; ULONG fetched=0; char b[160];
    DWORD regKey=0, start, elapsed; SDATA d; MSG msg;
    static WCHAR text[]=L"test";
    CoInitialize(NULL);
    if(FAILED(CoCreateInstance(&GUID_TTSEnumerator,NULL,CLSCTX_ALL,&GUID_ITTSEnumW,(void**)&en))||!en){ Out("no SAPI4\n"); ExitProcess(2); }
    en->lpVtbl->Reset(en);
    en->lpVtbl->Next(en,1,&g_mi,&fetched);
    if(fetched!=1){ Out("no voice\n"); ExitProcess(3); }
    { const WCHAR*s=g_mi.szModeName[0]?g_mi.szModeName:g_mi.szProductName; char nm[80]; int i; for(i=0;s[i]&&i<79;i++)nm[i]=(char)s[i]; nm[i]=0; Out("voice: "); Out(nm); Out("\n"); }

    /* Select with NULL audio destination */
    { HRESULT hr=en->lpVtbl->Select(en,g_mi.gModeID,&c,NULL);
      wsprintfA(b,"Select(NULL audio): hr=0x%08lx central=%s\n",(unsigned long)hr,c?"yes":"NO"); Out(b); }
    if(!c){ Out("RESULT: NULL select gives no central - NULL is NOT usable.\n"); ExitProcess(0); }

    if(SUCCEEDED(c->lpVtbl->QueryInterface(c,&GUID_ITTSAttributesW,(void**)&at))&&at){
        DWORD sp=0; at->lpVtbl->SpeedGet(at,&sp); wsprintfA(b,"params work: default speed=%lu\n",(unsigned long)sp); Out(b);
    }

    Out("speaking \"test\" with NULL audio (listen)...\n");
    c->lpVtbl->Register(c,(PVOID)&g_sink,GUID_ITTSNotifySinkW,&regKey);
    g_stopped=0;
    d.pData=(PVOID)text; d.dwSize=sizeof(text);
    start=GetTickCount();
    { HRESULT hr=c->lpVtbl->TextData(c,CHARSET_TEXT,TTSDATAFLAG_TAGGED,d,NULL,GUID_ITTSBufNotifySink);
      wsprintfA(b,"TextData hr=0x%08lx\n",(unsigned long)hr); Out(b); }
    while(!g_stopped){ while(PeekMessageA(&msg,NULL,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageA(&msg);} if(GetTickCount()-start>8000)break; Sleep(5); }
    elapsed=GetTickCount()-start;
    wsprintfA(b,"speak elapsed=%lu ms, stopped=%ld\n",(unsigned long)elapsed,(long)g_stopped); Out(b);
    Out(elapsed>=200 ? "RESULT: took real time -> NULL audio DOES play sound; usable.\n"
                     : "RESULT: near-instant -> NULL audio produced NO sound; not usable.\n");
    if(regKey) c->lpVtbl->UnRegister(c,regKey);
    if(at) at->lpVtbl->Release(at);
    c->lpVtbl->Release(c);
    en->lpVtbl->Release(en);
    ExitProcess(0);
}
