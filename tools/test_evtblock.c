/* test_evtblock.c - does ISpVoice::GetEvents block while another thread is in
 * the middle of a synchronous ISpVoice::Speak on the SAME voice?
 *
 * This reproduces, sound-free, the exact concurrency the app hit: the worker
 * thread renders (Speak), while the "UI" thread drains events (GetEvents).  If
 * GetEvents blocks for the duration of the render, the UI freezes - which is
 * why the app now skips event pumping during a background save.
 *
 * Output is set to a temp WAV so nothing is played through the speakers. */
#include <windows.h>
#define COBJMACROS
#include <sapi.h>

int _fltused = 0x9875;
#pragma function(memset, memcpy)
void *__cdecl memset(void *d,int v,size_t n){ unsigned char*p=(unsigned char*)d; while(n--)*p++=(unsigned char)v; return d; }
void *__cdecl memcpy(void *d,const void *s,size_t n){ unsigned char*a=(unsigned char*)d; const unsigned char*b=(const unsigned char*)s; while(n--)*a++=*b++; return d; }
void *__cdecl memmove(void *d,const void *s,size_t n){ return memcpy(d,s,n); }
int   __cdecl memcmp(const void*a,const void*b,size_t n){ const unsigned char*p=a,*q=b; while(n--){ if(*p!=*q) return *p-*q; p++;q++; } return 0; }

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }
static void OutN(const char *label, long v){ char b[64]; wsprintfA(b, "%s%ld\n", label, v); Out(b); }

static ISpVoice *g_voice;

static DWORD WINAPI RenderWorker(LPVOID p)
{
    /* very long text so the render lasts several seconds (file render runs
     * much faster than real time, so this has to be big) */
    static WCHAR big[400000];
    const WCHAR *unit = L"This is a long passage rendered to a file so the render lasts a while. ";
    int ulen = lstrlenW(unit), pos = 0;
    (void)p;
    while (pos + ulen < (int)(sizeof(big)/sizeof(big[0])) - 1) { lstrcpyW(big + pos, unit); pos += ulen; }
    big[pos] = 0;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    /* SPF_DEFAULT == synchronous: returns only when the whole render is done */
    ISpVoice_Speak(g_voice, big, SPF_DEFAULT, NULL);
    CoUninitialize();
    return 0;
}

void __cdecl WinMainCRTStartup(void)
{
    ISpStream   *stream = NULL;
    WAVEFORMATEX wfx;
    HANDLE       th;
    DWORD        startTick, maxGet = 0, calls = 0;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    if (FAILED(CoCreateInstance(&CLSID_SpVoice, NULL, CLSCTX_ALL,
                                &IID_ISpVoice, (void **)&g_voice))) {
        Out("no SAPI5\n"); ExitProcess(2);
    }
    ISpVoice_SetInterest(g_voice,
        SPFEI(SPEI_WORD_BOUNDARY) | SPFEI(SPEI_START_INPUT_STREAM) | SPFEI(SPEI_END_INPUT_STREAM),
        SPFEI(SPEI_WORD_BOUNDARY) | SPFEI(SPEI_START_INPUT_STREAM) | SPFEI(SPEI_END_INPUT_STREAM));

    /* Render to a temp WAV instead of the speakers (no sound). */
    ZeroMemory(&wfx, sizeof(wfx));
    wfx.wFormatTag = WAVE_FORMAT_PCM; wfx.nChannels = 1; wfx.nSamplesPerSec = 22050;
    wfx.wBitsPerSample = 16; wfx.nBlockAlign = 2; wfx.nAvgBytesPerSec = 22050*2;
    if (FAILED(CoCreateInstance(&CLSID_SpStream, NULL, CLSCTX_ALL,
                                &IID_ISpStream, (void **)&stream))) { Out("no SpStream\n"); ExitProcess(2); }
    ISpStream_BindToFile(stream, L"C:\\git\\Speakalive\\build\\evtblock.wav",
                         SPFM_CREATE_ALWAYS, &SPDFID_WaveFormatEx, &wfx, 0);
    ISpVoice_SetOutput(g_voice, (IUnknown *)stream, TRUE);

    /* Start the render on a worker, then hammer GetEvents from this thread and
     * time each call.  A big max means GetEvents stalls behind Speak. */
    th = CreateThread(NULL, 0, RenderWorker, NULL, 0, NULL);
    startTick = GetTickCount();
    for (;;) {
        SPEVENT ev; ULONG got = 0; DWORD t0, dt;
        if (WaitForSingleObject(th, 0) == WAIT_OBJECT_0) break;   /* render done */
        t0 = GetTickCount();
        while (ISpVoice_GetEvents(g_voice, 1, &ev, &got) == S_OK && got == 1) { }
        dt = GetTickCount() - t0;
        if (dt > maxGet) maxGet = dt;
        calls++;
        Sleep(30);
    }
    OutN("total render ms     : ", (long)(GetTickCount() - startTick));
    OutN("GetEvents calls     : ", (long)calls);
    OutN("slowest GetEvents ms: ", (long)maxGet);
    Out(maxGet < 250 ? "RESULT: GetEvents stays responsive\n"
                     : "RESULT: GetEvents BLOCKS behind Speak\n");

    ISpVoice_SetOutput(g_voice, NULL, TRUE);
    ISpStream_Release(stream);
    ISpVoice_Release(g_voice);
    CoUninitialize();
    ExitProcess(0);
}
