/* test_s5speed.c - why is our SAPI 5 file render slower than other apps?
 * Render the same slow voice the same text via several approaches and time it:
 *   A: our way - stream@22050 + WaitUntilDone(100) poll loop
 *   B: stream@22050 + WaitUntilDone(INFINITE)
 *   C: stream@44100 + WaitUntilDone(INFINITE)
 *   D: stream@48000 + WaitUntilDone(INFINITE)
 *   E: no SetOutput rate forced - default audio... (skipped, plays sound)
 * Picks the slowest voice it can find (TGSpeechBox / VW). */
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

static DWORD RenderOnce(DWORD rate, int pollLoop)
{
    ISpStream *st = NULL;
    WAVEFORMATEX wfx;
    DWORD t;
    ZeroMemory(&wfx, sizeof(wfx));
    wfx.wFormatTag=WAVE_FORMAT_PCM; wfx.nChannels=1; wfx.nSamplesPerSec=rate;
    wfx.wBitsPerSample=16; wfx.nBlockAlign=2; wfx.nAvgBytesPerSec=rate*2;
    CoCreateInstance(&CLSID_SpStream, NULL, CLSCTX_ALL, &IID_ISpStream, (void**)&st);
    ISpStream_BindToFile(st, L"C:\\git\\Speakalive\\build\\sp.wav",
                         SPFM_CREATE_ALWAYS, &SPDFID_WaveFormatEx, &wfx, 0);
    ISpVoice_SetOutput(v, (IUnknown*)st, TRUE);
    t = GetTickCount();
    ISpVoice_Speak(v, g_text, SPF_IS_NOT_XML | SPF_ASYNC, NULL);
    if (pollLoop) { while (ISpVoice_WaitUntilDone(v, 100) == S_FALSE) {} }
    else          { ISpVoice_WaitUntilDone(v, INFINITE); }
    t = GetTickCount() - t;
    ISpStream_Close(st); ISpStream_Release(st);
    return t;
}

void __cdecl WinMainCRTStartup(void)
{
    ISpObjectTokenCategory *cat = NULL;
    IEnumSpObjectTokens *en = NULL;
    ISpObjectToken *chosen = NULL;
    ULONG count = 0, i;
    const WCHAR *unit = L"This is Seediffusion, a brand by a blind software developer. ";
    int ulen = wlen(unit), p = 0, k;
    char nm[100]; WCHAR *d = NULL;

    while (p + ulen < 3000) { for(k=0;unit[k];k++) g_text[p+k]=unit[k]; p+=ulen; }
    g_text[p]=0;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CoCreateInstance(&CLSID_SpVoice, NULL, CLSCTX_ALL, &IID_ISpVoice, (void**)&v);
    CoCreateInstance(&CLSID_SpObjectTokenCategory, NULL, CLSCTX_ALL,
                     &IID_ISpObjectTokenCategory, (void**)&cat);
    ISpObjectTokenCategory_SetId(cat, SPCAT_VOICES, FALSE);
    ISpObjectTokenCategory_EnumTokens(cat, NULL, NULL, &en);
    IEnumSpObjectTokens_GetCount(en, &count);
    /* prefer TGSpeechBox, else VW */
    for (i = 0; i < count; i++) {
        ISpObjectToken *tok = NULL; ULONG got = 0; WCHAR *nd = NULL;
        if (IEnumSpObjectTokens_Next(en, 1, &tok, &got) == S_OK && got == 1) {
            if (SUCCEEDED(ISpObjectToken_GetStringValue(tok, NULL, &nd)) && nd) {
                if (startsWith(nd, "TGSpeechBox")) { if(chosen) ISpObjectToken_Release(chosen); chosen=tok; ISpObjectToken_AddRef(tok); CoTaskMemFree(nd); ISpObjectToken_Release(tok); break; }
                if (startsWith(nd, "VW") && !chosen) { chosen=tok; ISpObjectToken_AddRef(tok); }
                CoTaskMemFree(nd);
            }
            ISpObjectToken_Release(tok);
        }
    }
    if (!chosen) { Out("no slow voice found\n"); ExitProcess(2); }
    ISpVoice_SetVoice(v, chosen);
    if (SUCCEEDED(ISpObjectToken_GetStringValue(chosen, NULL, &d)) && d) {
        for(i=0;d[i]&&i<99;i++) nm[i]=(char)d[i]; nm[i]=0; CoTaskMemFree(d);
        Out("voice: "); Out(nm); Out("\n");
    }
    { char b[64]; wsprintfA(b,"text chars: %d\n", p); Out(b); }

    OutMs("A 22050 + poll loop(100)  = ", RenderOnce(22050, 1));
    OutMs("B 22050 + WaitInfinite    = ", RenderOnce(22050, 0));
    OutMs("C 44100 + WaitInfinite    = ", RenderOnce(44100, 0));
    OutMs("D 48000 + WaitInfinite    = ", RenderOnce(48000, 0));
    OutMs("B2 22050 + WaitInfinite   = ", RenderOnce(22050, 0));

    DeleteFileW(L"C:\\git\\Speakalive\\build\\sp.wav");
    ISpObjectToken_Release(chosen);
    IEnumSpObjectTokens_Release(en); ISpObjectTokenCategory_Release(cat);
    ISpVoice_Release(v);
    CoUninitialize();
    ExitProcess(0);
}
