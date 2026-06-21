/* test_traytime.c - time Shell_NotifyIcon NIM_ADD / NIM_MODIFY(NIF_INFO).
 * The icon is deleted immediately so no toast lingers on screen. */
#include <windows.h>
#include <shellapi.h>

int _fltused = 0x9875;
#pragma function(memset)
void *__cdecl memset(void *d,int v,size_t n){ unsigned char*p=(unsigned char*)d; while(n--)*p++=(unsigned char)v; return d; }

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }
static void OutMs(const char *l, DWORD ms){ char b[96]; wsprintfA(b,"%s%lu ms\n",l,(unsigned long)ms); Out(b); }

void __cdecl WinMainCRTStartup(void)
{
    WNDCLASSA wc;
    HWND h;
    NOTIFYICONDATAA nid;
    DWORD t;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc=DefWindowProcA; wc.hInstance=GetModuleHandleA(NULL);
    wc.lpszClassName="TrayTimeTest"; RegisterClassA(&wc);
    h = CreateWindowA("TrayTimeTest","",WS_OVERLAPPED,0,0,0,0,NULL,NULL,wc.hInstance,NULL);

    memset(&nid, 0, sizeof(nid));
    nid.cbSize=sizeof(nid); nid.hWnd=h; nid.uID=1;
    nid.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP; nid.uCallbackMessage=WM_APP+1;
    nid.hIcon=LoadIconA(NULL,(LPCSTR)IDI_APPLICATION);
    lstrcpyA(nid.szTip,"Speakalive");

    t = GetTickCount();
    Shell_NotifyIconA(NIM_ADD, &nid);
    OutMs("NIM_ADD            = ", GetTickCount() - t);

    nid.uFlags=NIF_INFO; nid.dwInfoFlags=NIIF_INFO;
    lstrcpyA(nid.szInfoTitle,"Speakalive");
    lstrcpyA(nid.szInfo,"Audio saved: test.wav");
    t = GetTickCount();
    Shell_NotifyIconA(NIM_MODIFY, &nid);
    OutMs("NIM_MODIFY(NIF_INFO)= ", GetTickCount() - t);

    t = GetTickCount();
    Shell_NotifyIconA(NIM_DELETE, &nid);
    OutMs("NIM_DELETE         = ", GetTickCount() - t);

    DestroyWindow(h);
    ExitProcess(0);
}
