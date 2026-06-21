/* test_s5voices.c - per-voice timing of a SAPI 5 file save, to find which
 * voices are slow and in which phase.  Renders to a temp WAV (no speakers). */
#include <windows.h>
#include <mmreg.h>
#define COBJMACROS
#include <sapi.h>

int _fltused = 0x9875;
#pragma function(memset, memcpy)
void *__cdecl memset(void *d,int v,size_t n){ unsigned char*p=(unsigned char*)d; while(n--)*p++=(unsigned char)v; return d; }
void *__cdecl memcpy(void *d,const void *s,size_t n){ unsigned char*a=(unsigned char*)d; const unsigned char*b=(const unsigned char*)s; while(n--)*a++=*b++; return d; }
static int wlen(const WCHAR*s){ int n=0; while(s[n]) n++; return n; }

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }

static WCHAR g_text[2200];

static void Render(ISpVoice *v, ISpObjectToken *tok)
{
    ISpStream *st = NULL;
    WAVEFORMATEX wfx;
    DWORD bind, render, revert;
    char line[200], name[100];
    WCHAR *desc = NULL;
    int i;

    ZeroMemory(&wfx, sizeof(wfx));
    wfx.wFormatTag=WAVE_FORMAT_PCM; wfx.nChannels=1; wfx.nSamplesPerSec=22050;
    wfx.wBitsPerSample=16; wfx.nBlockAlign=2; wfx.nAvgBytesPerSec=44100;

    { DWORD t=GetTickCount(); ISpVoice_SetOutput(v, NULL, TRUE); bind=GetTickCount()-t; }

    CoCreateInstance(&CLSID_SpStream, NULL, CLSCTX_ALL, &IID_ISpStream, (void**)&st);
    ISpStream_BindToFile(st, L"C:\\git\\Speakalive\\build\\v.wav",
                         SPFM_CREATE_ALWAYS, &SPDFID_WaveFormatEx, &wfx, 0);
    ISpVoice_SetOutput(v, (IUnknown*)st, TRUE);
    { DWORD t=GetTickCount();
      ISpVoice_Speak(v, g_text, SPF_IS_NOT_XML | SPF_ASYNC, NULL);
      ISpVoice_WaitUntilDone(v, INFINITE);
      render=GetTickCount()-t; }
    { DWORD t=GetTickCount(); ISpVoice_SetOutput(v, NULL, TRUE); revert=GetTickCount()-t; }
    ISpStream_Close(st); ISpStream_Release(st);

    name[0]=0;
    if (SUCCEEDED(ISpObjectToken_GetStringValue(tok, NULL, &desc)) && desc) {
        for (i=0; desc[i] && i<99; i++) name[i]=(char)desc[i]; name[i]=0;
        CoTaskMemFree(desc);
    }
    wsprintfA(line, "bind=%4lu render=%5lu revert=%4lu ms  %s\n",
              (unsigned long)bind,(unsigned long)render,(unsigned long)revert,name);
    Out(line);
}

void __cdecl WinMainCRTStartup(void)
{
    ISpVoice *v = NULL;
    ISpObjectTokenCategory *cat = NULL;
    IEnumSpObjectTokens *en = NULL;
    ULONG count = 0, i;
    const WCHAR *unit = L"This is Seediffusion, a brand by a blind developer. ";
    int ulen = wlen(unit), p = 0, k;

    while (p + ulen < 260) { for(k=0;unit[k];k++) g_text[p+k]=unit[k]; p+=ulen; }
    g_text[p]=0;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(CoCreateInstance(&CLSID_SpVoice, NULL, CLSCTX_ALL, &IID_ISpVoice, (void**)&v))) {
        Out("no SAPI5\n"); ExitProcess(1);
    }

    if (FAILED(CoCreateInstance(&CLSID_SpObjectTokenCategory, NULL, CLSCTX_ALL,
                                &IID_ISpObjectTokenCategory, (void**)&cat)) ||
        FAILED(ISpObjectTokenCategory_SetId(cat, SPCAT_VOICES, FALSE)) ||
        FAILED(ISpObjectTokenCategory_EnumTokens(cat, NULL, NULL, &en)) || !en) {
        Out("could not enumerate voices\n"); ExitProcess(1);
    }
    IEnumSpObjectTokens_GetCount(en, &count);
    for (i = 0; i < count; i++) {
        ISpObjectToken *tok = NULL; ULONG got = 0;
        if (IEnumSpObjectTokens_Next(en, 1, &tok, &got) == S_OK && got == 1) {
            ISpVoice_SetVoice(v, tok);
            Render(v, tok);
            ISpObjectToken_Release(tok);
        }
    }
    IEnumSpObjectTokens_Release(en);
    ISpObjectTokenCategory_Release(cat);

    DeleteFileW(L"C:\\git\\Speakalive\\build\\v.wav");
    ISpVoice_Release(v);
    CoUninitialize();
    ExitProcess(0);
}
