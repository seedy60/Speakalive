/* test_tray.c - confirm the notification-area struct (V2 size, as compiled for
 * Win2000) is accepted by the shell.  Adds and immediately removes a tray icon;
 * it does NOT fire a balloon, so nothing pops up on screen. */
#include <windows.h>
#include <shellapi.h>

int _fltused = 0x9875;
#pragma function(memset)
void *__cdecl memset(void *d,int v,size_t n){ unsigned char*p=(unsigned char*)d; while(n--)*p++=(unsigned char)v; return d; }

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }

void __cdecl WinMainCRTStartup(void)
{
    WNDCLASSA wc;
    HWND h;
    NOTIFYICONDATAA nid;
    BOOL added, removed;
    char line[80];

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = DefWindowProcA;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.lpszClassName = "SpeakaliveTrayTest";
    RegisterClassA(&wc);
    h = CreateWindowA("SpeakaliveTrayTest", "", WS_OVERLAPPED, 0, 0, 0, 0,
                      NULL, NULL, wc.hInstance, NULL);

    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd   = h;
    nid.uID    = 1;
    nid.uFlags = NIF_ICON | NIF_TIP;
    nid.hIcon  = LoadIconA(NULL, (LPCSTR)IDI_APPLICATION);
    lstrcpyA(nid.szTip, "test");

    wsprintfA(line, "sizeof(NOTIFYICONDATAA) = %d (V2 size expected)\n", (int)sizeof(nid));
    Out(line);

    added   = Shell_NotifyIconA(NIM_ADD, &nid);
    removed = Shell_NotifyIconA(NIM_DELETE, &nid);
    Out(added ? "NIM_ADD: accepted (struct/cbSize OK)\n" : "NIM_ADD: REJECTED\n");
    Out(removed ? "NIM_DELETE: ok\n" : "NIM_DELETE: failed\n");

    DestroyWindow(h);
    ExitProcess(added ? 0 : 1);
}
