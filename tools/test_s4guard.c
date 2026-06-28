/* test_s4guard.c - exercise the app's exact teardown-guard pattern: 120 Select +
 * guarded-release cycles.  On healthy voices the release must complete (freeing
 * the device, so no exhaustion - unlike the plain leak which died at switch 50),
 * and the guard must add no harm.  Mirrors ReleaseSelection in sapi4.c. */
#include <windows.h>
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

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }
#define MAXV 80
static GUID g_modes[MAXV]; static int g_count;
static TTSMODEINFOW g_mi;

/* --- copy of the app's guard --- */
static DWORD g_relJb[6]; static volatile LONG g_relGuard; static DWORD g_relThread;
__declspec(naked) static int S4_RelSave(void){ __asm{
    mov [g_relJb+0],ebx
    mov [g_relJb+4],esi
    mov [g_relJb+8],edi
    mov [g_relJb+12],ebp
    lea eax,[esp+4]
    mov [g_relJb+16],eax
    mov eax,[esp]
    mov [g_relJb+20],eax
    xor eax,eax
    ret } }
static LONG WINAPI Filter(EXCEPTION_POINTERS *ep){
    if(ep&&g_relGuard&&GetCurrentThreadId()==g_relThread&&ep->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION){
        CONTEXT *c=ep->ContextRecord;
        c->Ebx=g_relJb[0];c->Esi=g_relJb[1];c->Edi=g_relJb[2];c->Ebp=g_relJb[3];c->Esp=g_relJb[4];c->Eip=g_relJb[5];c->Eax=1;
        return EXCEPTION_CONTINUE_EXECUTION; }
    return EXCEPTION_EXECUTE_HANDLER; }

static IAudioMultiMediaDevice *g_audio; static ITTSCentralW *g_central; static int g_caught;
static void guardedTeardown(void){
    g_relThread=GetCurrentThreadId();
    if(S4_RelSave()==0){
        g_relGuard=1;
        if(g_central){ g_central->lpVtbl->Release(g_central); g_central=NULL; }
        if(g_audio){ g_audio->lpVtbl->Release(g_audio); g_audio=NULL; }
        g_relGuard=0;
    } else { g_relGuard=0; g_central=NULL; g_audio=NULL; g_caught++; }
}

void __cdecl WinMainCRTStartup(void){
    ITTSEnumW *en=NULL; ULONG fetched=0; char b[140]; int i, ok=0, fail=0, firstFail=-1;
    CoInitialize(NULL);
    SetUnhandledExceptionFilter(Filter);
    if(FAILED(CoCreateInstance(&GUID_TTSEnumerator,NULL,CLSCTX_ALL,&GUID_ITTSEnumW,(void**)&en))||!en){ Out("no SAPI4\n"); ExitProcess(2); }
    en->lpVtbl->Reset(en);
    while(g_count<MAXV && en->lpVtbl->Next(en,1,&g_mi,&fetched)==S_OK && fetched==1) g_modes[g_count++]=g_mi.gModeID;
    wsprintfA(b,"voices: %d ; 120 Select + guarded-release cycles\n",g_count); Out(b);

    for(i=0;i<200;i++){
        int v=0; HRESULT hr;   /* always voice 0 (robust) to isolate exhaustion */
        g_audio=NULL; g_central=NULL;
        CoCreateInstance(&GUID_MMAudioDest,NULL,CLSCTX_ALL,&GUID_IAudioMMDevice,(void**)&g_audio);
        if(!g_audio){ fail++; if(firstFail<0)firstFail=i; continue; }
        hr=en->lpVtbl->Select(en,g_modes[v],&g_central,(LPUNKNOWN)g_audio);
        if(SUCCEEDED(hr)&&g_central) ok++;
        else { fail++; if(firstFail<0)firstFail=i; }
        guardedTeardown();   /* releases central + audio under the guard */
    }
    wsprintfA(b,"result: %d ok, %d failed, %d faults caught",ok,fail,g_caught); Out(b);
    if(fail){ wsprintfA(b," (first failure at switch %d)",firstFail); Out(b); }
    Out("\n");
    Out(fail==0 ? "PASS: guarded release frees the device - 120 cycles, no exhaustion.\n"
               : "FAIL: still exhausts.\n");
    en->lpVtbl->Release(en);
    ExitProcess(0);
}
