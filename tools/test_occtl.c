/* test_occtl.c - verify OneCore pause/resume/stop work now that MCI is owned
 * by the (message-pumping) UI thread.  Speak from a window thread, let the
 * worker hand back the audio via WM_SA_OCPLAY, then control playback. */
#include <windows.h>
#include "../src/engine.h"

int _fltused = 0x9875;
#pragma function(memset, memcpy)
void *__cdecl memset(void *d,int v,size_t n){ unsigned char*p=(unsigned char*)d; while(n--)*p++=(unsigned char)v; return d; }
void *__cdecl memcpy(void *d,const void *s,size_t n){ unsigned char*a=(unsigned char*)d; const unsigned char*b=(const unsigned char*)s; while(n--)*a++=*b++; return d; }
void *__cdecl memmove(void *d,const void *s,size_t n){ return memcpy(d,s,n); }
int   __cdecl memcmp(const void *a,const void *b,size_t n){ const unsigned char*p=(const unsigned char*)a,*q=(const unsigned char*)b; while(n--){ if(*p!=*q) return (int)*p-(int)*q; p++;q++; } return 0; }

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }
static void Line(const char *label, BOOL v){ Out(label); Out(v ? ": TRUE\n" : ": FALSE\n"); }

static LRESULT CALLBACK WP(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_SA_OCPLAY) { OneCore_DoPlay((char *)w, (long)l); return 0; }
    return DefWindowProcA(h, m, w, l);
}
static void Pump(void){ MSG msg; while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); } }
static void PumpFor(DWORD ms){ DWORD t = GetTickCount(); while (GetTickCount() - t < ms) { Pump(); Sleep(10); } }

void __cdecl WinMainCRTStartup(void)
{
    WNDCLASSA wc;
    HWND hwnd;
    SpeechEngine *e;
    HINSTANCE hi = GetModuleHandleA(NULL);

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = WP; wc.hInstance = hi; wc.lpszClassName = "OcCtlTest";
    RegisterClassA(&wc);
    hwnd = CreateWindowExA(0, "OcCtlTest", "", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hi, NULL);

    e = OneCore_Get();
    if (!e->Detect()) { Out("OneCore not present\n"); ExitProcess(1); }
    e->Init(e);

    Out("speaking...\n");
    e->Speak(e, "This is a fairly long sentence so that there is plenty of audio to "
                "pause, resume, and then stop while it is playing in the background.",
             FALSE, hwnd);
    PumpFor(2500);                      /* let synthesis finish + WM_SA_OCPLAY play */

    Line("IsSpeaking after play", e->IsSpeaking(e));
    Line("Pause",  e->Pause(e));
    PumpFor(600);
    Line("Resume", e->Resume(e));
    PumpFor(600);
    Line("Stop",   e->Stop(e));
    PumpFor(200);

    e->Shutdown(e);
    Out("DONE\n");
    CoUninitialize();
    ExitProcess(0);
}
