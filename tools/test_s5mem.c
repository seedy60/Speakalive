/* test_s5mem.c - is rendering straight to a FILE stream the bottleneck?
 * Compare file-backed vs memory-backed SpStream render time for a slow voice. */
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
static ISpVoice *v;

static void fillFmt(WAVEFORMATEX *w){ ZeroMemory(w,sizeof(*w));
    w->wFormatTag=WAVE_FORMAT_PCM; w->nChannels=1; w->nSamplesPerSec=22050;
    w->wBitsPerSample=16; w->nBlockAlign=2; w->nAvgBytesPerSec=44100; }

static DWORD RenderFile(void)
{
    ISpStream *st=NULL; WAVEFORMATEX wfx; DWORD t; fillFmt(&wfx);
    CoCreateInstance(&CLSID_SpStream, NULL, CLSCTX_ALL, &IID_ISpStream, (void**)&st);
    ISpStream_BindToFile(st, L"C:\\git\\Speakalive\\build\\m.wav",
                         SPFM_CREATE_ALWAYS, &SPDFID_WaveFormatEx, &wfx, 0);
    ISpVoice_SetOutput(v,(IUnknown*)st,TRUE);
    t=GetTickCount();
    ISpVoice_Speak(v,g_text,SPF_IS_NOT_XML|SPF_ASYNC,NULL);
    ISpVoice_WaitUntilDone(v,INFINITE);
    t=GetTickCount()-t;
    ISpStream_Close(st); ISpStream_Release(st);
    return t;
}

static DWORD RenderMem(void)
{
    ISpStream *st=NULL; IStream *base=NULL; WAVEFORMATEX wfx; DWORD t; fillFmt(&wfx);
    CreateStreamOnHGlobal(NULL, TRUE, &base);
    CoCreateInstance(&CLSID_SpStream, NULL, CLSCTX_ALL, &IID_ISpStream, (void**)&st);
    ISpStream_SetBaseStream(st, base, &SPDFID_WaveFormatEx, &wfx);
    ISpVoice_SetOutput(v,(IUnknown*)st,TRUE);
    t=GetTickCount();
    ISpVoice_Speak(v,g_text,SPF_IS_NOT_XML|SPF_ASYNC,NULL);
    ISpVoice_WaitUntilDone(v,INFINITE);
    t=GetTickCount()-t;
    ISpStream_Release(st);
    if (base) base->lpVtbl->Release(base);
    return t;
}

void __cdecl WinMainCRTStartup(void)
{
    ISpObjectTokenCategory *cat=NULL; IEnumSpObjectTokens *en=NULL; ISpObjectToken *chosen=NULL;
    ULONG count=0,i; const WCHAR *unit=L"This is Seediffusion, a brand by a blind software developer. ";
    int ulen=wlen(unit),p=0,k; char nm[100]; WCHAR *d=NULL;

    while (p+ulen<3000){ for(k=0;unit[k];k++) g_text[p+k]=unit[k]; p+=ulen; }
    g_text[p]=0;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CoCreateInstance(&CLSID_SpVoice, NULL, CLSCTX_ALL, &IID_ISpVoice, (void**)&v);
    CoCreateInstance(&CLSID_SpObjectTokenCategory, NULL, CLSCTX_ALL,
                     &IID_ISpObjectTokenCategory, (void**)&cat);
    ISpObjectTokenCategory_SetId(cat, SPCAT_VOICES, FALSE);
    ISpObjectTokenCategory_EnumTokens(cat, NULL, NULL, &en);
    IEnumSpObjectTokens_GetCount(en,&count);
    for (i=0;i<count;i++){ ISpObjectToken *tok=NULL; ULONG got=0; WCHAR *nd=NULL;
        if (IEnumSpObjectTokens_Next(en,1,&tok,&got)==S_OK && got==1){
            if (SUCCEEDED(ISpObjectToken_GetStringValue(tok,NULL,&nd))&&nd){
                if (startsWith(nd,"TGSpeechBox")){ chosen=tok; ISpObjectToken_AddRef(tok); CoTaskMemFree(nd); ISpObjectToken_Release(tok); break; }
                if (startsWith(nd,"VW")&&!chosen){ chosen=tok; ISpObjectToken_AddRef(tok); }
                CoTaskMemFree(nd);
            }
            ISpObjectToken_Release(tok);
        }
    }
    if (!chosen){ Out("no slow voice found\n"); ExitProcess(2); }
    ISpVoice_SetVoice(v,chosen);
    if (SUCCEEDED(ISpObjectToken_GetStringValue(chosen,NULL,&d))&&d){ for(i=0;d[i]&&i<99;i++) nm[i]=(char)d[i]; nm[i]=0; CoTaskMemFree(d); Out("voice: "); Out(nm); Out("\n"); }
    { char b[64]; wsprintfA(b,"text chars: %d\n",p); Out(b); }

    OutMs("file stream   = ", RenderFile());
    OutMs("memory stream = ", RenderMem());
    OutMs("file stream   = ", RenderFile());
    OutMs("memory stream = ", RenderMem());

    DeleteFileW(L"C:\\git\\Speakalive\\build\\m.wav");
    ISpObjectToken_Release(chosen);
    IEnumSpObjectTokens_Release(en); ISpObjectTokenCategory_Release(cat);
    ISpVoice_Release(v);
    CoUninitialize();
    ExitProcess(0);
}
