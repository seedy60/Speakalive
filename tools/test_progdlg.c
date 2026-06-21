/* test_progdlg.c - instantiate the IDD_PROGRESS dialog from the built exe and
 * confirm the native progress bar control is created and responsive.  The
 * dialog template has no WS_VISIBLE, so CreateDialog builds it HIDDEN - nothing
 * appears on screen. */
#include <windows.h>
#include <commctrl.h>
#include "../src/resource.h"

int _fltused = 0x9875;
#pragma function(memset, memcpy)
void *__cdecl memset(void *d,int v,size_t n){ unsigned char*p=(unsigned char*)d; while(n--)*p++=(unsigned char)v; return d; }
void *__cdecl memcpy(void *d,const void *s,size_t n){ unsigned char*a=(unsigned char*)d; const unsigned char*b=(const unsigned char*)s; while(n--)*a++=*b++; return d; }

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }

static HWND g_bar;
static int  g_pos = -1;
static char g_cls[64];

static INT_PTR CALLBACK TestProc(HWND dlg, UINT m, WPARAM w, LPARAM l)
{
    (void)w; (void)l;
    if (m == WM_INITDIALOG) {
        g_bar = GetDlgItem(dlg, IDC_PROGRESSBAR);
        if (g_bar) {
            GetClassNameA(g_bar, g_cls, sizeof(g_cls));
            SendMessageA(g_bar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            SendMessageA(g_bar, PBM_SETPOS, 42, 0);
            g_pos = (int)SendMessageA(g_bar, PBM_GETPOS, 0, 0);
        }
        return TRUE;
    }
    return FALSE;
}

void __cdecl WinMainCRTStartup(void)
{
    INITCOMMONCONTROLSEX icc;
    HMODULE mod;
    HWND    dlg;
    char    line[160];

    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icc);

    mod = LoadLibraryExA("C:\\git\\Speakalive\\build\\Speakalive.exe", NULL,
                         LOAD_LIBRARY_AS_DATAFILE);
    if (!mod) { Out("could not load Speakalive.exe for its resources\n"); ExitProcess(1); }

    dlg = CreateDialogParamA(mod, MAKEINTRESOURCEA(IDD_PROGRESS), NULL, TestProc, 0);

    wsprintfA(line, "dialog created : %s\nprogress bar   : %s (class=%s)\nSetPos 42 -> GetPos %d\n",
        dlg ? "yes" : "NO", g_bar ? "yes" : "NO", g_cls, g_pos);
    Out(line);
    Out((dlg && g_bar && g_pos == 42) ? "RESULT: progress dialog + native bar OK\n"
                                      : "RESULT: FAIL\n");

    if (dlg) DestroyWindow(dlg);
    FreeLibrary(mod);
    ExitProcess(0);
}
