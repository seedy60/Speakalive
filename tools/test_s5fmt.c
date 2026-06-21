/* test_s5fmt.c - discover the SAPI 5 default voice's native output sample rate
 * and confirm how fAllowFormatChanges behaves.  Renders to files (no sound).
 *
 *   SetOutput(stream, TRUE)  -> SAPI adapts the output to the ENGINE format,
 *                               so the file ends up at the voice's NATIVE rate
 *                               regardless of the rate we asked the stream for.
 *   SetOutput(stream, FALSE) -> engine is converted to the stream's rate.
 */
#include <windows.h>
#include <mmreg.h>
#define COBJMACROS
#include <sapi.h>

int _fltused = 0x9875;
#pragma function(memset, memcpy)
void *__cdecl memset(void *d,int v,size_t n){ unsigned char*p=(unsigned char*)d; while(n--)*p++=(unsigned char)v; return d; }
void *__cdecl memcpy(void *d,const void *s,size_t n){ unsigned char*a=(unsigned char*)d; const unsigned char*b=(const unsigned char*)s; while(n--)*a++=*b++; return d; }

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }
static void OutN(const char *l, long v){ char b[80]; wsprintfA(b,"%s%ld\n",l,v); Out(b); }

/* read nSamplesPerSec (offset 24) from a RIFF/WAVE file */
static DWORD WavRate(const WCHAR *path)
{
    HANDLE h; BYTE b[64]; DWORD got = 0;
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    ReadFile(h, b, sizeof(b), &got, NULL);
    CloseHandle(h);
    if (got < 28) return 0;
    return (DWORD)b[24] | ((DWORD)b[25]<<8) | ((DWORD)b[26]<<16) | ((DWORD)b[27]<<24);
}

static DWORD RenderAt(ISpVoice *v, DWORD askRate, BOOL allowFmt, const WCHAR *path)
{
    ISpStream *st = NULL;
    WAVEFORMATEX wfx;
    ZeroMemory(&wfx, sizeof(wfx));
    wfx.wFormatTag=WAVE_FORMAT_PCM; wfx.nChannels=1; wfx.nSamplesPerSec=askRate;
    wfx.wBitsPerSample=16; wfx.nBlockAlign=2; wfx.nAvgBytesPerSec=askRate*2;
    if (FAILED(CoCreateInstance(&CLSID_SpStream, NULL, CLSCTX_ALL, &IID_ISpStream, (void**)&st)))
        return 0;
    if (SUCCEEDED(ISpStream_BindToFile(st, path, SPFM_CREATE_ALWAYS, &SPDFID_WaveFormatEx, &wfx, 0))) {
        ISpVoice_SetOutput(v, (IUnknown*)st, allowFmt);
        ISpVoice_Speak(v, L"Checking the output format.", SPF_DEFAULT, NULL);
        ISpVoice_SetOutput(v, NULL, TRUE);
        ISpStream_Close(st);
    }
    ISpStream_Release(st);
    return WavRate(path);
}

void __cdecl WinMainCRTStartup(void)
{
    ISpVoice *v = NULL;
    const WCHAR *p1=L"C:\\git\\Speakalive\\build\\fmt1.wav";
    const WCHAR *p2=L"C:\\git\\Speakalive\\build\\fmt2.wav";
    const WCHAR *p3=L"C:\\git\\Speakalive\\build\\fmt3.wav";

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(CoCreateInstance(&CLSID_SpVoice, NULL, CLSCTX_ALL, &IID_ISpVoice, (void**)&v))) {
        Out("no SAPI5\n"); ExitProcess(1);
    }

    OutN("ask 8000  + allowFmt=TRUE  -> file rate = ", (long)RenderAt(v, 8000,  TRUE,  p1));
    OutN("ask 48000 + allowFmt=TRUE  -> file rate = ", (long)RenderAt(v, 48000, TRUE,  p2));
    OutN("ask 22050 + allowFmt=FALSE -> file rate = ", (long)RenderAt(v, 22050, FALSE, p3));
    Out("(the TRUE rows reveal the voice's NATIVE rate; they should match each other)\n");

    DeleteFileW(p1); DeleteFileW(p2); DeleteFileW(p3);
    ISpVoice_Release(v);
    CoUninitialize();
    ExitProcess(0);
}
