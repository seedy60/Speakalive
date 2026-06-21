/* test_s5par.c - does the slow voice's engine allow concurrent rendering?
 * Two threads each render the full text with their own ISpVoice to memory.
 * If the engine parallelizes, both finish in ~1x the single-render time; if it
 * serializes, ~2x.  This tells us whether parallel chunk rendering would help. */
#include <windows.h>
#include <mmreg.h>
#define COBJMACROS
#include <sapi.h>

int _fltused = 0x9875;
#pragma function(memset, memcpy)
void *__cdecl memset(void *d,int v,size_t n){ unsigned char*p=(unsigned char*)d; while(n--)*p++=(unsigned char)v; return d; }
void *__cdecl memcpy(void *d,const void *s,size_t n){ unsigned char*a=(unsigned char*)d; const unsigned char*b=(const unsigned char*)s; while(n--)*a++=*b++; return d; }
static int wlen(const WCHAR*s){ int n=0; while(s[n]) n++; return n; }
static int startsWith(const WCHAR*s,const char*p){ int i; for(i=0;p[i];i++) if((char)s[i]!=p[i]) return 0; return 1; }

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }
static void OutMs(const char *l, DWORD ms){ char b[96]; wsprintfA(b,"%s%lu ms\n",l,(unsigned long)ms); Out(b); }

static WCHAR g_text[3200];
static WCHAR g_tokenId[260];   /* the slow voice's token id */

/* Render the full text once with a fresh voice on this thread. */
static DWORD WINAPI Worker(LPVOID arg)
{
    ISpVoice *v=NULL; ISpObjectToken *tok=NULL; ISpStream *st=NULL; IStream *base=NULL;
    WAVEFORMATEX wfx;
    (void)arg;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CoCreateInstance(&CLSID_SpVoice, NULL, CLSCTX_ALL, &IID_ISpVoice, (void**)&v);
    CoCreateInstance(&CLSID_SpObjectToken, NULL, CLSCTX_ALL, &IID_ISpObjectToken, (void**)&tok);
    if (tok) { ISpObjectToken_SetId(tok, NULL, g_tokenId, FALSE); ISpVoice_SetVoice(v, tok); }
    ZeroMemory(&wfx,sizeof(wfx)); wfx.wFormatTag=WAVE_FORMAT_PCM; wfx.nChannels=1;
    wfx.nSamplesPerSec=22050; wfx.wBitsPerSample=16; wfx.nBlockAlign=2; wfx.nAvgBytesPerSec=44100;
    CreateStreamOnHGlobal(NULL, TRUE, &base);
    CoCreateInstance(&CLSID_SpStream, NULL, CLSCTX_ALL, &IID_ISpStream, (void**)&st);
    ISpStream_SetBaseStream(st, base, &SPDFID_WaveFormatEx, &wfx);
    ISpVoice_SetOutput(v,(IUnknown*)st,TRUE);
    ISpVoice_Speak(v,g_text,SPF_IS_NOT_XML|SPF_ASYNC,NULL);
    ISpVoice_WaitUntilDone(v,INFINITE);
    ISpStream_Release(st); if(base) base->lpVtbl->Release(base);
    if(tok) ISpObjectToken_Release(tok);
    ISpVoice_Release(v);
    CoUninitialize();
    return 0;
}

void __cdecl WinMainCRTStartup(void)
{
    ISpObjectTokenCategory *cat=NULL; IEnumSpObjectTokens *en=NULL;
    ULONG count=0,i; const WCHAR *unit=L"This is Seediffusion, a brand by a blind software developer. ";
    int ulen=wlen(unit),p=0,k; char nm[100]; WCHAR *d=NULL; DWORD t; HANDLE th[2];

    while (p+ulen<3000){ for(k=0;unit[k];k++) g_text[p+k]=unit[k]; p+=ulen; }
    g_text[p]=0;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CoCreateInstance(&CLSID_SpObjectTokenCategory, NULL, CLSCTX_ALL,
                     &IID_ISpObjectTokenCategory, (void**)&cat);
    ISpObjectTokenCategory_SetId(cat, SPCAT_VOICES, FALSE);
    ISpObjectTokenCategory_EnumTokens(cat, NULL, NULL, &en);
    IEnumSpObjectTokens_GetCount(en,&count);
    for (i=0;i<count;i++){ ISpObjectToken *tok=NULL; ULONG got=0; WCHAR *nd=NULL; WCHAR *id=NULL;
        if (IEnumSpObjectTokens_Next(en,1,&tok,&got)==S_OK && got==1){
            if (SUCCEEDED(ISpObjectToken_GetStringValue(tok,NULL,&nd))&&nd){
                int pick = startsWith(nd,"TGSpeechBox") || (startsWith(nd,"VW") && g_tokenId[0]==0);
                if (pick && SUCCEEDED(ISpObjectToken_GetId(tok,&id)) && id){
                    int j; for(j=0;id[j]&&j<259;j++) g_tokenId[j]=id[j]; g_tokenId[j]=0;
                    for(j=0;nd[j]&&j<99;j++) nm[j]=(char)nd[j]; nm[j]=0;
                    CoTaskMemFree(id);
                    if (startsWith(nd,"TGSpeechBox")){ CoTaskMemFree(nd); ISpObjectToken_Release(tok); break; }
                }
                CoTaskMemFree(nd);
            }
            ISpObjectToken_Release(tok);
        }
    }
    IEnumSpObjectTokens_Release(en); ISpObjectTokenCategory_Release(cat);
    if (!g_tokenId[0]){ Out("no slow voice found\n"); ExitProcess(2); }
    Out("voice: "); Out(nm); Out("\n");
    { char b[64]; wsprintfA(b,"text chars: %d\n",p); Out(b); }

    /* one render */
    t=GetTickCount(); { HANDLE h=CreateThread(NULL,0,Worker,NULL,0,NULL); WaitForSingleObject(h,INFINITE); CloseHandle(h); }
    OutMs("1 thread (1x text)        = ", GetTickCount()-t);

    /* two concurrent renders of the full text */
    t=GetTickCount();
    th[0]=CreateThread(NULL,0,Worker,NULL,0,NULL);
    th[1]=CreateThread(NULL,0,Worker,NULL,0,NULL);
    WaitForMultipleObjects(2, th, TRUE, INFINITE);
    CloseHandle(th[0]); CloseHandle(th[1]);
    OutMs("2 threads (2x text total) = ", GetTickCount()-t);
    Out("(if ~= 1-thread time, the engine renders in parallel)\n");

    CoUninitialize();
    ExitProcess(0);
}
