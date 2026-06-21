/* test_getstatus.c - confirm ISpVoice::GetStatus gives real synthesis progress
 * during a file render, on a slow voice (VW Ashley), without crashing.  This is
 * polling, NOT the word-boundary events that crashed that voice. */
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
static void OutN(const char *l, long v){ char b[80]; wsprintfA(b,"%s%ld\n",l,v); Out(b); }

static WCHAR g_text[9000];

void __cdecl WinMainCRTStartup(void)
{
    ISpVoice *v = NULL;
    ISpObjectTokenCategory *cat = NULL;
    IEnumSpObjectTokens *en = NULL;
    ISpObjectToken *chosen = NULL;
    ULONG count = 0, i;
    ISpStream *st = NULL;
    WAVEFORMATEX wfx;
    SPVOICESTATUS status;
    int total, p = 0, k, samples = 0;
    const WCHAR *unit = L"This is Seediffusion, a brand by a blind software developer. ";
    int ulen = wlen(unit);

    while (p + ulen < 8800) { for(k=0;unit[k];k++) g_text[p+k]=unit[k]; p+=ulen; }
    g_text[p]=0;
    total = p;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CoCreateInstance(&CLSID_SpVoice, NULL, CLSCTX_ALL, &IID_ISpVoice, (void**)&v);

    /* find VW Ashley */
    CoCreateInstance(&CLSID_SpObjectTokenCategory, NULL, CLSCTX_ALL,
                     &IID_ISpObjectTokenCategory, (void**)&cat);
    ISpObjectTokenCategory_SetId(cat, SPCAT_VOICES, FALSE);
    ISpObjectTokenCategory_EnumTokens(cat, NULL, NULL, &en);
    IEnumSpObjectTokens_GetCount(en, &count);
    for (i = 0; i < count && !chosen; i++) {
        ISpObjectToken *tok = NULL; ULONG got = 0; WCHAR *d = NULL;
        if (IEnumSpObjectTokens_Next(en, 1, &tok, &got) == S_OK && got == 1) {
            if (SUCCEEDED(ISpObjectToken_GetStringValue(tok, NULL, &d)) && d) {
                if (d[0]=='V'&&d[1]=='W'&&d[3]=='A') { chosen = tok; ISpObjectToken_AddRef(tok); }
                CoTaskMemFree(d);
            }
            ISpObjectToken_Release(tok);
        }
    }
    if (!chosen) { Out("VW Ashley not found\n"); ExitProcess(2); }
    ISpVoice_SetVoice(v, chosen);
    OutN("text length chars = ", (long)total);

    ZeroMemory(&wfx, sizeof(wfx));
    wfx.wFormatTag=WAVE_FORMAT_PCM; wfx.nChannels=1; wfx.nSamplesPerSec=22050;
    wfx.wBitsPerSample=16; wfx.nBlockAlign=2; wfx.nAvgBytesPerSec=44100;
    CoCreateInstance(&CLSID_SpStream, NULL, CLSCTX_ALL, &IID_ISpStream, (void**)&st);
    ISpStream_BindToFile(st, L"C:\\git\\Speakalive\\build\\gs.wav",
                         SPFM_CREATE_ALWAYS, &SPDFID_WaveFormatEx, &wfx, 0);
    ISpVoice_SetOutput(v, (IUnknown*)st, TRUE);

    ISpVoice_Speak(v, g_text, SPF_IS_NOT_XML | SPF_ASYNC, NULL);
    while (ISpVoice_WaitUntilDone(v, 80) == S_FALSE) {
        ZeroMemory(&status, sizeof(status));
        if (SUCCEEDED(ISpVoice_GetStatus(v, &status, NULL))) {
            char b[100];
            wsprintfA(b, "  progress: word pos %lu / %d  (%lu%%)\n",
                (unsigned long)status.ulInputWordPos, total,
                (unsigned long)(total ? status.ulInputWordPos*100/(ULONG)total : 0));
            Out(b);
            samples++;
        }
    }
    OutN("GetStatus samples taken = ", (long)samples);
    Out("RESULT: GetStatus polled through the render without crashing\n");

    ISpVoice_SetOutput(v, NULL, TRUE);
    ISpStream_Close(st); ISpStream_Release(st);
    DeleteFileW(L"C:\\git\\Speakalive\\build\\gs.wav");
    ISpObjectToken_Release(chosen);
    ISpVoice_Release(v);
    CoUninitialize();
    ExitProcess(0);
}
