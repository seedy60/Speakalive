/* test_s5phases.c - time the phases at the END of a SAPI 5 file save to find
 * the "stuck near the end" delay.  Renders a short phrase to a temp WAV (no
 * speaker audio during the render) and times the revert + close. */
#include <windows.h>
#include <mmreg.h>
#define COBJMACROS
#include <sapi.h>

int _fltused = 0x9875;
#pragma function(memset, memcpy)
void *__cdecl memset(void *d,int v,size_t n){ unsigned char*p=(unsigned char*)d; while(n--)*p++=(unsigned char)v; return d; }
void *__cdecl memcpy(void *d,const void *s,size_t n){ unsigned char*a=(unsigned char*)d; const unsigned char*b=(const unsigned char*)s; while(n--)*a++=*b++; return d; }

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }
static void OutMs(const char *l, DWORD ms){ char b[96]; wsprintfA(b,"%s%lu ms\n",l,(unsigned long)ms); Out(b); }

void __cdecl WinMainCRTStartup(void)
{
    ISpVoice  *v = NULL;
    ISpStream *st = NULL;
    WAVEFORMATEX wfx;
    DWORD t;
    int   i;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(CoCreateInstance(&CLSID_SpVoice, NULL, CLSCTX_ALL, &IID_ISpVoice, (void**)&v))) {
        Out("no SAPI5\n"); ExitProcess(1);
    }

    /* Mimic S5_Init / S5_SetVoice: bind live output to the voice (native). */
    t = GetTickCount();
    ISpVoice_SetOutput(v, NULL, TRUE);
    OutMs("initial SetOutput(NULL,TRUE) cold = ", GetTickCount() - t);

    for (i = 0; i < 2; i++) {
        Out(i == 0 ? "--- save #1 ---\n" : "--- save #2 ---\n");
        ZeroMemory(&wfx, sizeof(wfx));
        wfx.wFormatTag=WAVE_FORMAT_PCM; wfx.nChannels=1; wfx.nSamplesPerSec=22050;
        wfx.wBitsPerSample=16; wfx.nBlockAlign=2; wfx.nAvgBytesPerSec=44100;
        CoCreateInstance(&CLSID_SpStream, NULL, CLSCTX_ALL, &IID_ISpStream, (void**)&st);
        ISpStream_BindToFile(st, L"C:\\git\\Speakalive\\build\\phase.wav",
                             SPFM_CREATE_ALWAYS, &SPDFID_WaveFormatEx, &wfx, 0);

        ISpVoice_SetOutput(v, (IUnknown*)st, TRUE);
        ISpVoice_Speak(v, L"Short render.", SPF_IS_NOT_XML | SPF_ASYNC, NULL);
        while (ISpVoice_WaitUntilDone(v, 100) == S_FALSE) {}

        t = GetTickCount();
        ISpVoice_SetOutput(v, NULL, TRUE);          /* the revert in S5_Save */
        OutMs("  revert SetOutput(NULL,TRUE) = ", GetTickCount() - t);

        t = GetTickCount();
        ISpStream_Close(st);
        OutMs("  ISpStream_Close            = ", GetTickCount() - t);
        ISpStream_Release(st); st = NULL;
    }

    DeleteFileW(L"C:\\git\\Speakalive\\build\\phase.wav");
    ISpVoice_Release(v);
    CoUninitialize();
    ExitProcess(0);
}
