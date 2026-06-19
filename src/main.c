/* main.c - Speakalive user interface (native Win32, accessible, Win2000+). */
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>

#include "resource.h"
#include "engine.h"
#include "util.h"
#include "webread.h"

#define APP_CLASS "SpeakaliveWindow"
#define APP_TITLE "Speakalive"

/* Posted by the web-fetch worker thread; wParam is a heap text string or NULL. */
#define WM_SA_WEBDONE (WM_APP + 300)

/* Slider ranges (normalised inside the engines). */
#define RATE_MIN   0
#define RATE_MAX   20
#define RATE_DEF   10   /* 10 -> engine 0 (default speed)  */
#define PITCH_MIN  0
#define PITCH_MAX  20
#define PITCH_DEF  10
#define VOL_MIN    0
#define VOL_MAX    100
#define VOL_DEF    100

/* Dark theme colours. */
#define DARK_BG   RGB(32,32,32)
#define DARK_CTL  RGB(45,45,45)
#define DARK_TXT  RGB(240,240,240)
#define DARK_BTN  RGB(60,60,60)
#define DARK_EDGE RGB(100,100,100)
#define DARK_DIS  RGB(128,128,128)

/* ---- module state ---------------------------------------------------- */

static HINSTANCE g_inst;
static HWND g_main, g_tab, g_voiceLbl, g_voice, g_textLbl, g_text;
static HWND g_rateLbl, g_rate, g_rateVal, g_pitchLbl, g_pitch, g_pitchVal;
static HWND g_volLbl, g_volume, g_volVal, g_reset, g_speak, g_pause, g_stop, g_status;
static HFONT g_font;
static HMENU g_menu;

static WNDPROC g_editOldProc;      /* original edit window proc  */

static SpeechEngine *g_engine;     /* current engine            */
static int  g_engineIndex = -1;
static BOOL g_xmlMode = TRUE;      /* honour SAPI 5 XML / SSML  */
static BOOL g_highlight = FALSE;   /* select the spoken word in the text box.
                                    * Off by default: the selection changes make
                                    * a screen reader talk over the speech. */
static BOOL g_speaking = FALSE;
static BOOL g_paused   = FALSE;
static BOOL g_saving   = FALSE;    /* guards re-entry during a file render */
static BOOL g_webBusy  = FALSE;    /* a web page fetch is in flight        */
static HWND g_lastFocus = NULL;    /* control to restore focus to on return*/

/* Settings loaded from the registry at start-up. */
static int  g_haveSettings = 0;
static int  g_loadRate, g_loadPitch, g_loadVol;
static int  g_loadDark, g_loadFollowOs;
static char g_loadEngine[32];
static char g_loadVoice[256];
static BOOL g_dark     = FALSE;    /* dark theme active                   */
static BOOL g_followOs = FALSE;    /* follow the OS light/dark setting     */
static HBRUSH g_brBg   = NULL;     /* window background brush (dark)       */
static HBRUSH g_brCtl  = NULL;     /* control background brush (dark)      */
static char g_statusText[160];

/* ---- small helpers --------------------------------------------------- */

static void SetStatus(const char *text)
{
    if (text != g_statusText) lstrcpynA(g_statusText, text, sizeof(g_statusText));
    if (g_status) {
        /* In dark mode the part is owner-drawn so we can paint light text. */
        SendMessageA(g_status, SB_SETTEXTA,
                     (WPARAM)(g_dark ? SBT_OWNERDRAW : 0), (LPARAM)g_statusText);
    }
    /* The title bar stays a plain "Speakalive" - status lives only in the
     * status bar so it doesn't churn the window title. */
}

static void FormatSigned(int v, char *buf, int len)
{
    if (v > 0) { buf[0] = '+'; IntToStr(v, buf + 1, len - 1); }
    else IntToStr(v, buf, len);
}

static int TbPos(HWND tb) { return (int)SendMessageA(tb, TBM_GETPOS, 0, 0); }

static char *GetEditText(void)
{
    int len = GetWindowTextLengthA(g_text);
    char *buf = (char *)Mem_Alloc((SIZE_T)len + 1);
    if (!buf) return NULL;
    if (len) GetWindowTextA(g_text, buf, len + 1);
    buf[len] = 0;
    return buf;
}

/* ---- value labels ---------------------------------------------------- */

static void UpdateValueLabels(void)
{
    char b[16];
    FormatSigned(TbPos(g_rate) - RATE_DEF, b, sizeof(b));
    SetWindowTextA(g_rateVal, b);
    FormatSigned(TbPos(g_pitch) - PITCH_DEF, b, sizeof(b));
    SetWindowTextA(g_pitchVal, b);
    IntToStr(TbPos(g_volume), b, sizeof(b));
    lstrcatA(b, "%");
    SetWindowTextA(g_volVal, b);
}

static void ApplyAllSliders(void)
{
    if (!g_engine) return;
    if (g_engine->SetRate)   g_engine->SetRate(g_engine, TbPos(g_rate) - RATE_DEF);
    if (g_engine->SetPitch)  g_engine->SetPitch(g_engine, TbPos(g_pitch) - PITCH_DEF);
    if (g_engine->SetVolume) g_engine->SetVolume(g_engine, TbPos(g_volume));
    UpdateValueLabels();
}

/* ---- voices ---------------------------------------------------------- */

static void PopulateVoices(void)
{
    Voice *v = NULL;
    int n = 0, i;
    SendMessageA(g_voice, CB_RESETCONTENT, 0, 0);
    if (g_engine && g_engine->GetVoices) n = g_engine->GetVoices(g_engine, &v);
    for (i = 0; i < n; i++)
        SendMessageA(g_voice, CB_ADDSTRING, 0, (LPARAM)v[i].name);
    if (n > 0) {
        SendMessageA(g_voice, CB_SETCURSEL, 0, 0);
        if (g_engine->SetVoice) g_engine->SetVoice(g_engine, 0);
    }
    EnableWindow(g_voice, n > 0);
}

/* ---- engine switching ------------------------------------------------ */

static void SwitchEngine(int index)
{
    SpeechEngine *e = Engines_Get(index);
    if (!e) return;

    /* Stop whatever the previous engine was doing. */
    if (g_engine && g_engine->Stop) g_engine->Stop(g_engine);
    g_speaking = g_paused = FALSE;

    if (e->Init && !e->Init(e)) {
        SetStatus("Engine failed to initialise");
        return;
    }
    g_engine = e;
    g_engineIndex = index;

    PopulateVoices();
    ApplyAllSliders();

    /* Volume only applies to engines that support it (SAPI 5). */
    EnableWindow(g_volume, e->supportsVolume);
    EnableWindow(g_volLbl, e->supportsVolume);
    EnableWindow(g_volVal, e->supportsVolume);
    EnableWindow(g_pitch,  e->supportsPitch);
    EnableWindow(g_pitchLbl, e->supportsPitch);
    EnableWindow(g_pitchVal, e->supportsPitch);

    {
        char s[80];
        lstrcpyA(s, "Engine: ");
        lstrcatA(s, e->display);
        SetStatus(s);
    }
}

/* ---- actions --------------------------------------------------------- */

/* Cycle to the next (dir=+1) or previous (dir=-1) engine tab, wrapping. */
static void CycleEngine(int dir)
{
    int n = Engines_Count();
    int sel;
    if (n <= 1) return;
    sel = (int)SendMessageA(g_tab, TCM_GETCURSEL, 0, 0);
    sel = (sel + dir + n) % n;
    SendMessageA(g_tab, TCM_SETCURSEL, sel, 0);
    SwitchEngine(sel);   /* TCM_SETCURSEL does not raise TCN_SELCHANGE */
}

static void DoSpeak(void)
{
    char *text;
    if (g_saving) return;
    if (!g_engine || !g_engine->Speak) { MessageBeep(MB_ICONWARNING); return; }
    text = GetEditText();
    if (!text || text[0] == 0) {
        SetStatus("Nothing to speak - type some text first");
        MessageBeep(MB_ICONASTERISK);
        Mem_Free(text);
        return;
    }
    if (g_engine->Speak(g_engine, text, g_xmlMode, g_main)) {
        g_speaking = TRUE; g_paused = FALSE;
        SetStatus("Speaking");
    } else {
        SetStatus("Speech failed");
        MessageBeep(MB_ICONERROR);
    }
    Mem_Free(text);
}

static void DoPlayPause(void)
{
    if (g_saving) return;
    if (!g_engine) return;
    if (!g_speaking) { DoSpeak(); return; }
    if (!g_paused) {
        if (g_engine->Pause) g_engine->Pause(g_engine);
        g_paused = TRUE; SetStatus("Paused");
    } else {
        if (g_engine->Resume) g_engine->Resume(g_engine);
        g_paused = FALSE; SetStatus("Speaking");
    }
}

static void DoStop(void)
{
    if (g_engine && g_engine->Stop) g_engine->Stop(g_engine);
    g_speaking = g_paused = FALSE;
    SetStatus("Stopped");
}

static void DoReset(void)
{
    SendMessageA(g_rate,   TBM_SETPOS, TRUE, RATE_DEF);
    SendMessageA(g_pitch,  TBM_SETPOS, TRUE, PITCH_DEF);
    SendMessageA(g_volume, TBM_SETPOS, TRUE, VOL_DEF);
    ApplyAllSliders();
    SetStatus("Sliders reset to default");
}

static void DoSave(void)
{
    OPENFILENAMEA ofn;
    char file[MAX_PATH];
    char *text;
    int  fmt, channels, ans, n;

    if (g_saving) return;
    if (!g_engine || !g_engine->SaveToFile) {
        MessageBoxA(g_main, "Saving to a file is not supported for this engine.",
                    APP_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }
    text = GetEditText();
    if (!text || text[0] == 0) {
        MessageBoxA(g_main, "Type some text to save first.", APP_TITLE,
                    MB_OK | MB_ICONINFORMATION);
        Mem_Free(text);
        return;
    }

    file[0] = 0;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_main;
    ofn.lpstrFilter = "Wave audio (*.wav)\0*.wav\0MP3 audio (*.mp3)\0*.mp3\0";
    ofn.nFilterIndex= 1;
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = "Save spoken audio";
    ofn.lpstrDefExt = "wav";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST |
                OFN_NOCHANGEDIR;
    if (!GetSaveFileNameA(&ofn)) { Mem_Free(text); return; }

    /* Format from filter index, overridden by an explicit .mp3 extension. */
    fmt = (ofn.nFilterIndex == 2) ? FMT_MP3 : FMT_WAV;
    n = lstrlenA(file);
    if (n > 4) {
        const char *ext = file + n - 4;
        if (lstrcmpiA(ext, ".mp3") == 0) fmt = FMT_MP3;
        else if (lstrcmpiA(ext, ".wav") == 0) fmt = FMT_WAV;
    }

    ans = MessageBoxA(g_main,
                      "Save in stereo?\n\nYes = stereo (2 channels)\nNo = mono (1 channel)",
                      "Channels", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (ans == IDCANCEL) { Mem_Free(text); return; }
    channels = (ans == IDYES) ? 2 : 1;

    SetStatus("Saving audio...");
    g_saving = TRUE;
    EnableWindow(g_main, FALSE);   /* modal while the engine renders */
    ans = g_engine->SaveToFile(g_engine, text, g_xmlMode, file, fmt, channels);
    EnableWindow(g_main, TRUE);
    g_saving = FALSE;
    if (ans) {
        SetStatus("Audio saved");
        MessageBoxA(g_main, "Audio file saved successfully.", APP_TITLE,
                    MB_OK | MB_ICONINFORMATION);
    } else {
        SetStatus("Save failed");
        MessageBoxA(g_main,
            fmt == FMT_MP3 ?
            "Could not save the MP3 file.\n\nMP3 saving needs an MP3 ACM encoder "
            "or lame.exe (place lame.exe next to Speakalive.exe). The WAV format "
            "always works." :
            "Could not save the audio file.",
            APP_TITLE, MB_OK | MB_ICONERROR);
    }
    Mem_Free(text);
}

static void DoSaveText(void)
{
    OPENFILENAMEA ofn;
    char  file[MAX_PATH];
    char *text;
    HANDLE h;
    DWORD  written;
    int    n;

    if (g_saving) return;
    text = GetEditText();

    file[0] = 0;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_main;
    ofn.lpstrFilter = "Text files (*.txt)\0*.txt\0All files (*.*)\0*.*\0";
    ofn.nFilterIndex= 1;
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = "Save text";
    ofn.lpstrDefExt = "txt";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST |
                OFN_NOCHANGEDIR;
    if (!GetSaveFileNameA(&ofn)) { Mem_Free(text); return; }

    h = CreateFileA(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        n = text ? lstrlenA(text) : 0;
        if (n) WriteFile(h, text, (DWORD)n, &written, NULL);
        CloseHandle(h);
        SetStatus("Text saved");
    } else {
        SetStatus("Could not save text");
        MessageBoxA(g_main, "Could not save the text file.", APP_TITLE,
                    MB_OK | MB_ICONERROR);
    }
    Mem_Free(text);
}

/* ---- read a web page ------------------------------------------------- */

/* Defined later (dark-mode section); the URL dialog reuses them. */
static void DrawButton(const DRAWITEMSTRUCT *di);
static void SetTitleBarDark(HWND h, BOOL on);

static INT_PTR CALLBACK UrlDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtrA(dlg, DWLP_USER, (LONG_PTR)lp);  /* output buffer */
        SendMessageA(dlg, DM_SETDEFID, IDOK, 0);          /* Enter = OK     */
        SetTitleBarDark(dlg, g_dark);
        SetFocus(GetDlgItem(dlg, IDC_URLEDIT));
        return FALSE;

    /* Apply the program's dark theme to the dialog. */
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        if (g_dark) {
            SetTextColor((HDC)wp, DARK_TXT);
            SetBkColor((HDC)wp, DARK_BG);
            return (INT_PTR)g_brBg;
        }
        break;
    case WM_CTLCOLOREDIT:
        if (g_dark) {
            SetTextColor((HDC)wp, DARK_TXT);
            SetBkColor((HDC)wp, DARK_CTL);
            return (INT_PTR)g_brCtl;
        }
        break;
    case WM_DRAWITEM:
        DrawButton((const DRAWITEMSTRUCT *)lp);
        return TRUE;

    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            char *buf = (char *)(LONG_PTR)GetWindowLongPtrA(dlg, DWLP_USER);
            if (buf) GetDlgItemTextA(dlg, IDC_URLEDIT, buf, 2048);
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) { EndDialog(dlg, IDCANCEL); return TRUE; }
        break;
    }
    return FALSE;
}

/* Runs off the UI thread so a slow page never freezes the window. */
static DWORD WINAPI WebThread(LPVOID param)
{
    char *url  = (char *)param;
    char *text = Web_FetchText(url);
    Mem_Free(url);
    PostMessageA(g_main, WM_SA_WEBDONE, (WPARAM)text, 0);
    return 0;
}

static void DoWebPage(void)
{
    char  url[2048];
    char *s, *full;
    int   n, i, hasScheme = 0;

    if (g_webBusy) { MessageBeep(MB_ICONASTERISK); return; }
    url[0] = 0;
    if (DialogBoxParamA(g_inst, MAKEINTRESOURCEA(IDD_URL), g_main,
                        UrlDlgProc, (LPARAM)url) != IDOK)
        return;

    s = url;
    while (*s == ' ' || *s == '\t') s++;        /* trim leading spaces */
    if (*s == 0) return;

    n = lstrlenA(s);
    for (i = 0; i + 2 < n; i++)
        if (s[i] == ':' && s[i + 1] == '/' && s[i + 2] == '/') { hasScheme = 1; break; }

    if (hasScheme) {
        full = StrDupA(s);
    } else {
        full = (char *)Mem_Alloc((SIZE_T)n + 8);
        if (full) { lstrcpyA(full, "http://"); lstrcatA(full, s); }
    }
    if (!full) return;

    g_webBusy = TRUE;
    SetStatus("Loading web page...");
    {
        HANDLE th = CreateThread(NULL, 0, WebThread, full, 0, NULL);
        if (th) CloseHandle(th);
        else { Mem_Free(full); g_webBusy = FALSE; SetStatus("Could not start loading"); }
    }
}

static void DoAbout(void)
{
    MessageBoxA(g_main,
        APP_TITLE " - a native Win32 text-to-speech program.\n\n"
        "Engines: SAPI 4, SAPI 5 and Windows OneCore (whichever are present).\n"
        "Hotkeys: F5 speak, F6 play/pause, F7 stop.\n"
        "Markup: SAPI 4 control tags and SAPI 5 XML / SSML are supported when\n"
        "\"Speak as XML / SSML\" is enabled.\n\n"
        "Designed to be fully keyboard and screen-reader accessible.",
        "About " APP_TITLE, MB_OK | MB_ICONINFORMATION);
}

/* ---- layout ---------------------------------------------------------- */

static void MoveCtl(HWND h, int x, int y, int w, int ht)
{
    SetWindowPos(h, NULL, x, y, w, ht, SWP_NOZORDER | SWP_NOACTIVATE);
}

static void Layout(int cw, int ch)
{
    const int M = 8, lblW = 70, valW = 56, rowH = 30, ctlH = 24;
    int statusH = 22, btnH = 26, sliderH;
    int buttonsY, slidersY, textTop, textBottom, sx, sw;
    int stopX, pauseX, speakX;
    RECT rc;

    if (g_status) {
        GetWindowRect(g_status, &rc);
        statusH = rc.bottom - rc.top;
        SendMessageA(g_status, WM_SIZE, 0, 0);
    }

    buttonsY = ch - statusH - M - btnH;
    sliderH  = rowH * 3;
    slidersY = buttonsY - M - sliderH;

    MoveCtl(g_tab,      M, M, cw - 2 * M, 26);
    MoveCtl(g_voiceLbl, M, 42, lblW, ctlH);
    MoveCtl(g_voice,    M + lblW + 6, 40, cw - 2 * M - lblW - 6, 220);
    MoveCtl(g_textLbl,  M, 70, cw - 2 * M, 18);

    textTop = 90;
    textBottom = slidersY - M;
    if (textBottom < textTop + 40) textBottom = textTop + 40;
    MoveCtl(g_text, M, textTop, cw - 2 * M, textBottom - textTop);

    sx = M + lblW + 6;
    sw = cw - 2 * M - lblW - 6 - valW - 6;
    if (sw < 60) sw = 60;
    MoveCtl(g_rateLbl,  M, slidersY + 6,           lblW, ctlH);
    MoveCtl(g_rate,     sx, slidersY,              sw, ctlH);
    MoveCtl(g_rateVal,  sx + sw + 6, slidersY + 6, valW, ctlH);
    MoveCtl(g_pitchLbl, M, slidersY + rowH + 6,    lblW, ctlH);
    MoveCtl(g_pitch,    sx, slidersY + rowH,       sw, ctlH);
    MoveCtl(g_pitchVal, sx + sw + 6, slidersY + rowH + 6, valW, ctlH);
    MoveCtl(g_volLbl,   M, slidersY + 2 * rowH + 6, lblW, ctlH);
    MoveCtl(g_volume,   sx, slidersY + 2 * rowH,   sw, ctlH);
    MoveCtl(g_volVal,   sx + sw + 6, slidersY + 2 * rowH + 6, valW, ctlH);

    MoveCtl(g_reset, M, buttonsY, 130, btnH);
    stopX  = cw - M - 90;
    pauseX = stopX - 6 - 130;
    speakX = pauseX - 6 - 110;
    if (speakX < M + 140) speakX = M + 140;
    MoveCtl(g_speak, speakX, buttonsY, 110, btnH);
    MoveCtl(g_pause, pauseX, buttonsY, 130, btnH);
    MoveCtl(g_stop,  stopX,  buttonsY, 90,  btnH);
}

/* ---- control creation ------------------------------------------------ */

/* The multi-line edit asks for all keys (DLGC_WANTALLKEYS), so IsDialogMessage
 * never gets to navigate away from it.  Subclass it to restore Tab / Shift+Tab
 * navigation and to add Ctrl+A "select all" (which multi-line edits lack). */
static LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_KEYDOWN:
        if (w == VK_TAB) {
            BOOL prev = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            HWND next = GetNextDlgTabItem(g_main, h, prev);
            if (next) SetFocus(next);
            return 0;
        }
        if (w == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            SendMessageA(h, EM_SETSEL, 0, (LPARAM)-1);
            return 0;
        }
        break;
    case WM_CHAR:
        /* Swallow the characters those keys would otherwise insert. */
        if (w == '\t' || w == 1 /* Ctrl+A */) return 0;
        break;
    }
    return CallWindowProcA(g_editOldProc, h, m, w, l);
}

static HWND Child(const char *cls, const char *text, DWORD style, int id)
{
    HWND h = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             0, 0, 10, 10, g_main, (HMENU)(INT_PTR)id, g_inst, NULL);
    if (h && g_font) SendMessageA(h, WM_SETFONT, (WPARAM)g_font, TRUE);
    return h;
}

static void CreateControls(void)
{
    int i, n;

    g_tab = Child(WC_TABCONTROLA, "", WS_TABSTOP, IDC_TAB);

    g_voiceLbl = Child("STATIC", "&Voice:", SS_LEFT | SS_NOTIFY, IDC_VOICELBL);
    g_voice    = Child("COMBOBOX", "", WS_TABSTOP | WS_VSCROLL |
                       CBS_DROPDOWNLIST, IDC_VOICE);

    g_textLbl  = Child("STATIC", "Text to &speak:", SS_LEFT | SS_NOTIFY, IDC_TEXTLBL);
    g_text     = Child("EDIT", "",
                       WS_TABSTOP | WS_BORDER | WS_VSCROLL | ES_MULTILINE |
                       ES_AUTOVSCROLL | ES_WANTRETURN | ES_NOHIDESEL, IDC_TEXT);
    g_editOldProc = (WNDPROC)(LONG_PTR)SetWindowLongPtrA(g_text, GWLP_WNDPROC,
                                                         (LONG_PTR)EditProc);
    /* Allow large content (E.G. a whole web page), not the default ~32 KB. */
    SendMessageA(g_text, EM_SETLIMITTEXT, (WPARAM)0x4000000, 0);

    g_rateLbl  = Child("STATIC", "&Rate:", SS_LEFT | SS_NOTIFY, IDC_RATELBL);
    g_rate     = Child(TRACKBAR_CLASSA, "Rate", WS_TABSTOP | TBS_HORZ |
                       TBS_AUTOTICKS, IDC_RATE);
    g_rateVal  = Child("STATIC", "0", SS_LEFT, IDC_RATEVAL);

    g_pitchLbl = Child("STATIC", "&Pitch:", SS_LEFT | SS_NOTIFY, IDC_PITCHLBL);
    g_pitch    = Child(TRACKBAR_CLASSA, "Pitch", WS_TABSTOP | TBS_HORZ |
                       TBS_AUTOTICKS, IDC_PITCH);
    g_pitchVal = Child("STATIC", "0", SS_LEFT, IDC_PITCHVAL);

    g_volLbl   = Child("STATIC", "Vol&ume:", SS_LEFT | SS_NOTIFY, IDC_VOLLBL);
    g_volume   = Child(TRACKBAR_CLASSA, "Volume", WS_TABSTOP | TBS_HORZ |
                       TBS_AUTOTICKS, IDC_VOLUME);
    g_volVal   = Child("STATIC", "100%", SS_LEFT, IDC_VOLVAL);

    g_reset = Child("BUTTON", "&Reset Sliders", WS_TABSTOP | BS_OWNERDRAW, IDC_RESET);
    g_speak = Child("BUTTON", "Spea&k (F5)", WS_TABSTOP | BS_OWNERDRAW, IDC_SPEAK);
    g_pause = Child("BUTTON", "Pla&y/Pause (F6)", WS_TABSTOP | BS_OWNERDRAW, IDC_PAUSE);
    g_stop  = Child("BUTTON", "S&top (F7)", WS_TABSTOP | BS_OWNERDRAW, IDC_STOP);

    g_status = CreateWindowExA(0, STATUSCLASSNAMEA, "", WS_CHILD | WS_VISIBLE |
                               SBARS_SIZEGRIP, 0, 0, 0, 0, g_main,
                               (HMENU)(INT_PTR)IDC_STATUS, g_inst, NULL);

    /* Slider ranges, ticks and starting positions. */
    SendMessageA(g_rate,   TBM_SETRANGE, TRUE, MAKELONG(RATE_MIN, RATE_MAX));
    SendMessageA(g_rate,   TBM_SETPOS,   TRUE, RATE_DEF);
    SendMessageA(g_rate,   TBM_SETTICFREQ, 5, 0);
    SendMessageA(g_pitch,  TBM_SETRANGE, TRUE, MAKELONG(PITCH_MIN, PITCH_MAX));
    SendMessageA(g_pitch,  TBM_SETPOS,   TRUE, PITCH_DEF);
    SendMessageA(g_pitch,  TBM_SETTICFREQ, 5, 0);
    SendMessageA(g_volume, TBM_SETRANGE, TRUE, MAKELONG(VOL_MIN, VOL_MAX));
    SendMessageA(g_volume, TBM_SETPOS,   TRUE, VOL_DEF);
    SendMessageA(g_volume, TBM_SETTICFREQ, 10, 0);

    /* Populate engine tabs. */
    n = Engines_Count();
    for (i = 0; i < n; i++) {
        TCITEMA ti;
        SpeechEngine *e = Engines_Get(i);
        ZeroMemory(&ti, sizeof(ti));
        ti.mask = TCIF_TEXT;
        ti.pszText = (char *)e->display;
        SendMessageA(g_tab, TCM_INSERTITEMA, i, (LPARAM)&ti);
    }
}

/* ---- window procedure ------------------------------------------------ */

static void HighlightWord(int start, int len)
{
    if (start < 0) start = 0;
    SendMessageA(g_text, EM_SETSEL, (WPARAM)start, (LPARAM)(start + len));
    SendMessageA(g_text, EM_SCROLLCARET, 0, 0);
}

/* ---- dark mode ------------------------------------------------------- */

/* Real OS version, unaffected by the missing app manifest. */
static BOOL IsWin10Plus(void)
{
    typedef LONG (WINAPI *PFN_RtlGetVersion)(void *);
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    if (nt) {
        PFN_RtlGetVersion f = (PFN_RtlGetVersion)GetProcAddress(nt, "RtlGetVersion");
        if (f) {
            OSVERSIONINFOW vi;
            ZeroMemory(&vi, sizeof(vi));
            vi.dwOSVersionInfoSize = sizeof(vi);
            f(&vi);
            return vi.dwMajorVersion >= 10;
        }
    }
    return FALSE;
}

/* The OS "apps use light theme" preference (Win10/11). */
static BOOL ReadOsDark(void)
{
    HKEY  hk;
    DWORD val = 1, sz = sizeof(val);
    BOOL  dark = FALSE;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
            "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_QUERY_VALUE, &hk) == ERROR_SUCCESS) {
        if (RegQueryValueExA(hk, "AppsUseLightTheme", NULL, NULL,
                             (BYTE *)&val, &sz) == ERROR_SUCCESS)
            dark = (val == 0);
        RegCloseKey(hk);
    }
    return dark;
}

/* Dark title bar on Windows 10 1809+ (ignored elsewhere). */
static void SetTitleBarDark(HWND h, BOOL on)
{
    typedef HRESULT (WINAPI *PFN_Dwm)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE dwm = LoadLibraryA("dwmapi.dll");
    if (dwm) {
        PFN_Dwm f = (PFN_Dwm)GetProcAddress(dwm, "DwmSetWindowAttribute");
        if (f) {
            BOOL v = on;
            if (FAILED(f(h, 20, &v, sizeof(v)))) f(h, 19, &v, sizeof(v));
        }
        FreeLibrary(dwm);
    }
}

static void DrawButton(const DRAWITEMSTRUCT *di)
{
    char txt[128];
    RECT rc = di->rcItem;
    BOOL pressed = (di->itemState & ODS_SELECTED) != 0;
    BOOL focus   = (di->itemState & ODS_FOCUS) != 0;
    BOOL disabled= (di->itemState & ODS_DISABLED) != 0;

    GetWindowTextA(di->hwndItem, txt, sizeof(txt));
    SetBkMode(di->hDC, TRANSPARENT);

    if (g_dark) {
        HBRUSH fill = CreateSolidBrush(pressed ? DARK_CTL : DARK_BTN);
        HPEN   pen  = CreatePen(PS_SOLID, 1, DARK_EDGE);
        HGDIOBJ op  = SelectObject(di->hDC, pen);
        HGDIOBJ ob  = SelectObject(di->hDC, fill);
        Rectangle(di->hDC, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(di->hDC, op);
        SelectObject(di->hDC, ob);
        DeleteObject(pen);
        DeleteObject(fill);
        SetTextColor(di->hDC, disabled ? DARK_DIS : DARK_TXT);
    } else {
        UINT s = DFCS_BUTTONPUSH | (pressed ? DFCS_PUSHED : 0) |
                 (disabled ? DFCS_INACTIVE : 0);
        DrawFrameControl(di->hDC, &rc, DFC_BUTTON, s);
        SetTextColor(di->hDC, GetSysColor(disabled ? COLOR_GRAYTEXT : COLOR_BTNTEXT));
    }
    if (pressed) OffsetRect(&rc, 1, 1);
    DrawTextA(di->hDC, txt, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (focus) {
        RECT fr = di->rcItem;
        InflateRect(&fr, -3, -3);
        DrawFocusRect(di->hDC, &fr);
    }
}

static void DrawStatusPart(const DRAWITEMSTRUCT *di)
{
    RECT   rc = di->rcItem;
    HBRUSH b  = CreateSolidBrush(DARK_BG);
    FillRect(di->hDC, &rc, b);
    DeleteObject(b);
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, DARK_TXT);
    rc.left += 2;
    if (di->itemData)
        DrawTextA(di->hDC, (const char *)di->itemData, -1, &rc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

static void ApplyDark(BOOL on)
{
    g_dark = on;
    if (g_brBg)  { DeleteObject(g_brBg);  g_brBg = NULL; }
    if (g_brCtl) { DeleteObject(g_brCtl); g_brCtl = NULL; }
    if (on) {
        g_brBg  = CreateSolidBrush(DARK_BG);
        g_brCtl = CreateSolidBrush(DARK_CTL);
    }
    if (g_status)
        SendMessageA(g_status, SB_SETBKCOLOR, 0, (LPARAM)(on ? DARK_BG : CLR_DEFAULT));
    SetStatus(g_statusText);   /* re-send with/without owner-draw flag */
    if (g_menu)
        CheckMenuItem(g_menu, IDM_DARKMODE, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
    if (g_main) {
        SetTitleBarDark(g_main, on);
        InvalidateRect(g_main, NULL, TRUE);
        RedrawWindow(g_main, NULL, NULL,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        /* Trackbars cache their channel background and don't repaint it on a
         * plain invalidate.  When the window is already visible (a live theme
         * toggle), nudge the size by a pixel so WM_SIZE forces a full relayout
         * and the channels repaint in the new colours. */
        if (IsWindowVisible(g_main)) {
            RECT wr;
            int  w, h;
            GetWindowRect(g_main, &wr);
            w = wr.right - wr.left;
            h = wr.bottom - wr.top;
            /* Width drives the slider width, so changing it forces the
             * trackbars to resize and repaint their cached channels. */
            SetWindowPos(g_main, NULL, 0, 0, w - 1, h,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            SetWindowPos(g_main, NULL, 0, 0, w, h,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
}

/* ---- settings persistence (registry) --------------------------------- */

#define SA_REGKEY "Software\\Speakalive"

static void RegSetDword(HKEY k, const char *name, DWORD v)
{
    RegSetValueExA(k, name, 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
}
static DWORD RegGetDword(HKEY k, const char *name, DWORD def)
{
    DWORD v = def, sz = sizeof(v), type = 0;
    if (RegQueryValueExA(k, name, NULL, &type, (BYTE *)&v, &sz) == ERROR_SUCCESS &&
        type == REG_DWORD)
        return v;
    return def;
}
static void RegSetStr(HKEY k, const char *name, const char *s)
{
    RegSetValueExA(k, name, 0, REG_SZ, (const BYTE *)s, (DWORD)lstrlenA(s) + 1);
}
static void RegGetStr(HKEY k, const char *name, char *buf, DWORD len)
{
    DWORD sz = len, type = 0;
    buf[0] = 0;
    if (RegQueryValueExA(k, name, NULL, &type, (BYTE *)buf, &sz) != ERROR_SUCCESS ||
        type != REG_SZ)
        buf[0] = 0;
    buf[len - 1] = 0;
}
static int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void LoadSettings(void)
{
    HKEY k;
    g_loadRate = RATE_DEF; g_loadPitch = PITCH_DEF; g_loadVol = VOL_DEF;
    g_loadDark = 0; g_loadFollowOs = 1;
    g_loadEngine[0] = 0; g_loadVoice[0] = 0;

    if (RegOpenKeyExA(HKEY_CURRENT_USER, SA_REGKEY, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
        return;
    g_haveSettings = 1;
    g_xmlMode   = RegGetDword(k, "XmlMode",   (DWORD)g_xmlMode)   ? TRUE : FALSE;
    g_highlight = RegGetDword(k, "Highlight", (DWORD)g_highlight) ? TRUE : FALSE;
    g_loadDark     = RegGetDword(k, "Dark", 0)     ? 1 : 0;
    g_loadFollowOs = RegGetDword(k, "FollowOs", 1) ? 1 : 0;
    g_loadRate  = ClampI((int)RegGetDword(k, "Rate",   RATE_DEF),  RATE_MIN,  RATE_MAX);
    g_loadPitch = ClampI((int)RegGetDword(k, "Pitch",  PITCH_DEF), PITCH_MIN, PITCH_MAX);
    g_loadVol   = ClampI((int)RegGetDword(k, "Volume", VOL_DEF),   VOL_MIN,   VOL_MAX);
    RegGetStr(k, "Engine", g_loadEngine, sizeof(g_loadEngine));
    RegGetStr(k, "Voice",  g_loadVoice,  sizeof(g_loadVoice));
    RegCloseKey(k);
}

static void SaveSettings(void)
{
    HKEY k;
    WINDOWPLACEMENT wp;
    int sel;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, SA_REGKEY, 0, NULL, 0, KEY_SET_VALUE,
                        NULL, &k, NULL) != ERROR_SUCCESS)
        return;
    RegSetDword(k, "XmlMode",   (DWORD)(g_xmlMode   ? 1 : 0));
    RegSetDword(k, "Highlight", (DWORD)(g_highlight ? 1 : 0));
    RegSetDword(k, "Dark",      (DWORD)(g_dark      ? 1 : 0));
    RegSetDword(k, "FollowOs",  (DWORD)(g_followOs  ? 1 : 0));
    RegSetDword(k, "Rate",   (DWORD)TbPos(g_rate));
    RegSetDword(k, "Pitch",  (DWORD)TbPos(g_pitch));
    RegSetDword(k, "Volume", (DWORD)TbPos(g_volume));
    if (g_engine && g_engine->id) RegSetStr(k, "Engine", g_engine->id);
    sel = (int)SendMessageA(g_voice, CB_GETCURSEL, 0, 0);
    if (sel >= 0) {
        int tl = (int)SendMessageA(g_voice, CB_GETLBTEXTLEN, sel, 0);
        if (tl > 0 && tl < 256) {
            char vn[256];
            SendMessageA(g_voice, CB_GETLBTEXT, sel, (LPARAM)vn);
            RegSetStr(k, "Voice", vn);
        }
    }
    ZeroMemory(&wp, sizeof(wp));
    wp.length = sizeof(wp);
    if (GetWindowPlacement(g_main, &wp))
        RegSetValueExA(k, "WindowPlacement", 0, REG_BINARY, (const BYTE *)&wp, sizeof(wp));
    RegCloseKey(k);
}

static void SelectVoiceByName(const char *name)
{
    int n = (int)SendMessageA(g_voice, CB_GETCOUNT, 0, 0), i;
    if (!name[0]) return;
    for (i = 0; i < n; i++) {
        int tl = (int)SendMessageA(g_voice, CB_GETLBTEXTLEN, i, 0);
        if (tl > 0 && tl < 256) {
            char vn[256];
            SendMessageA(g_voice, CB_GETLBTEXT, i, (LPARAM)vn);
            if (lstrcmpA(vn, name) == 0) {
                SendMessageA(g_voice, CB_SETCURSEL, i, 0);
                if (g_engine && g_engine->SetVoice) g_engine->SetVoice(g_engine, i);
                return;
            }
        }
    }
}

static int RestoreWindowPlacement(HWND h)
{
    HKEY k;
    WINDOWPLACEMENT wp;
    DWORD sz = sizeof(wp), type = 0;
    int ok = 0;
    ZeroMemory(&wp, sizeof(wp));
    if (RegOpenKeyExA(HKEY_CURRENT_USER, SA_REGKEY, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
        return 0;
    if (RegQueryValueExA(k, "WindowPlacement", NULL, &type, (BYTE *)&wp, &sz) == ERROR_SUCCESS &&
        type == REG_BINARY && sz == sizeof(wp) && wp.length == sizeof(wp)) {
        if (wp.showCmd == SW_SHOWMINIMIZED || wp.showCmd == SW_MINIMIZE)
            wp.showCmd = SW_SHOWNORMAL;        /* never start minimised */
        SetWindowPlacement(h, &wp);
        ok = 1;
    }
    RegCloseKey(k);
    return ok;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        int i, def = 0, count;
        LOGFONTA lf;
        g_main = hwnd;
        if (SystemParametersInfoA(SPI_GETICONTITLELOGFONT, sizeof(lf), &lf, 0))
            g_font = CreateFontIndirectA(&lf);
        if (!g_font) g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        CreateControls();
        LoadSettings();

        /* Restore the saved slider positions before SwitchEngine applies them. */
        SendMessageA(g_rate,   TBM_SETPOS, TRUE, g_loadRate);
        SendMessageA(g_pitch,  TBM_SETPOS, TRUE, g_loadPitch);
        SendMessageA(g_volume, TBM_SETPOS, TRUE, g_loadVol);

        count = Engines_Count();
        if (count == 0) {
            SetStatus("No speech engines found on this system");
            EnableWindow(g_speak, FALSE);
            EnableWindow(g_pause, FALSE);
            EnableWindow(g_stop, FALSE);
        } else {
            /* Prefer the saved engine, then SAPI 5, then the first present. */
            def = -1;
            if (g_loadEngine[0])
                for (i = 0; i < count; i++) {
                    SpeechEngine *e = Engines_Get(i);
                    if (e && e->id && lstrcmpA(e->id, g_loadEngine) == 0) { def = i; break; }
                }
            if (def < 0)
                for (i = 0; i < count; i++) {
                    SpeechEngine *e = Engines_Get(i);
                    if (e && e->id && lstrcmpA(e->id, "sapi5") == 0) { def = i; break; }
                }
            if (def < 0) def = 0;
            SendMessageA(g_tab, TCM_SETCURSEL, def, 0);
            SwitchEngine(def);
            SelectVoiceByName(g_loadVoice);
        }

        CheckMenuItem(g_menu, IDM_XML,
                      MF_BYCOMMAND | (g_xmlMode ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(g_menu, IDM_HIGHLIGHT,
                      MF_BYCOMMAND | (g_highlight ? MF_CHECKED : MF_UNCHECKED));

        /* Dark mode: on Win10/11 follow the OS setting by default (or the saved
         * choice); on older Windows "Follow OS" is unavailable and the saved
         * manual state is restored. */
        if (IsWin10Plus()) {
            g_followOs = g_haveSettings ? (g_loadFollowOs ? TRUE : FALSE) : TRUE;
            CheckMenuItem(g_menu, IDM_FOLLOWOS,
                          MF_BYCOMMAND | (g_followOs ? MF_CHECKED : MF_UNCHECKED));
        } else {
            g_followOs = FALSE;
            EnableMenuItem(g_menu, IDM_FOLLOWOS, MF_BYCOMMAND | MF_GRAYED);
        }
        ApplyDark(g_followOs ? ReadOsDark()
                             : (g_haveSettings && g_loadDark ? TRUE : FALSE));

        SetFocus(g_text);
        return 0;
    }

    case WM_SIZE:
        Layout(LOWORD(lp), HIWORD(lp));
        return 0;

    case WM_ACTIVATE:
        /* Remember which control had focus when we lose activation, and put it
         * back when we return (E.G. after Alt+Tab), instead of letting focus
         * default to an arbitrary control. */
        if (LOWORD(wp) == WA_INACTIVE) {
            HWND f = GetFocus();
            if (f && IsChild(hwnd, f)) g_lastFocus = f;
        } else if (g_lastFocus && IsWindow(g_lastFocus)) {
            SetFocus(g_lastFocus);
        }
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO *)lp;
        mmi->ptMinTrackSize.x = 520;
        mmi->ptMinTrackSize.y = 440;
        return 0;
    }

    case WM_ERASEBKGND:
        if (g_dark && g_brBg) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect((HDC)wp, &rc, g_brBg);
            return 1;
        }
        break;

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        if (g_dark) {
            SetTextColor((HDC)wp, DARK_TXT);
            SetBkColor((HDC)wp, DARK_CTL);
            return (LRESULT)g_brCtl;
        }
        break;

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        if (g_dark) {
            SetTextColor((HDC)wp, DARK_TXT);
            SetBkColor((HDC)wp, DARK_BG);
            return (LRESULT)g_brBg;
        }
        break;

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *di = (DRAWITEMSTRUCT *)lp;
        if (di->CtlType == ODT_BUTTON) { DrawButton(di); return TRUE; }
        if (di->hwndItem == g_status)  { DrawStatusPart(di); return TRUE; }
        break;
    }

    case WM_SETTINGCHANGE:
        /* Follow the OS light/dark preference when the user opted in. */
        if (g_followOs && lp &&
            lstrcmpiA((const char *)lp, "ImmersiveColorSet") == 0)
            ApplyDark(ReadOsDark());
        return 0;

    case WM_HSCROLL:
        if ((HWND)lp == g_rate || (HWND)lp == g_pitch || (HWND)lp == g_volume) {
            ApplyAllSliders();
        }
        return 0;

    case WM_NOTIFY: {
        LPNMHDR nh = (LPNMHDR)lp;
        if (nh->hwndFrom == g_tab && nh->code == TCN_SELCHANGE) {
            int sel = (int)SendMessageA(g_tab, TCM_GETCURSEL, 0, 0);
            SwitchEngine(sel);
            /* Keep focus on the tab control so the user can keep arrowing
             * through the engine tabs. */
        }
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wp);
        int code = HIWORD(wp);
        if ((HWND)lp == g_voice && code == CBN_SELCHANGE) {
            int sel = (int)SendMessageA(g_voice, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && g_engine && g_engine->SetVoice)
                g_engine->SetVoice(g_engine, sel);
            return 0;
        }
        switch (id) {
        case IDC_SPEAK: case IDM_SPEAK: DoSpeak();     return 0;
        case IDC_PAUSE: case IDM_PAUSE: DoPlayPause();  return 0;
        case IDC_STOP:  case IDM_STOP:  DoStop();       return 0;
        case IDM_NEXTENGINE: CycleEngine(+1);          return 0;
        case IDM_PREVENGINE: CycleEngine(-1);          return 0;
        case IDC_RESET: case IDM_RESET: DoReset();      return 0;
        case IDM_SAVE:                  DoSave();       return 0;
        case IDM_SAVETEXT:              DoSaveText();   return 0;
        case IDM_WEBPAGE:               DoWebPage();    return 0;
        case IDM_ABOUT:                 DoAbout();      return 0;
        case IDM_EXIT:  DestroyWindow(hwnd);            return 0;
        case IDM_XML:
            g_xmlMode = !g_xmlMode;
            CheckMenuItem(g_menu, IDM_XML,
                MF_BYCOMMAND | (g_xmlMode ? MF_CHECKED : MF_UNCHECKED));
            SetStatus(g_xmlMode ? "XML / SSML markup enabled"
                                : "XML / SSML markup disabled");
            return 0;
        case IDM_HIGHLIGHT:
            g_highlight = !g_highlight;
            CheckMenuItem(g_menu, IDM_HIGHLIGHT,
                MF_BYCOMMAND | (g_highlight ? MF_CHECKED : MF_UNCHECKED));
            SetStatus(g_highlight ? "Spoken word highlighting on"
                                  : "Spoken word highlighting off");
            return 0;
        case IDM_DARKMODE:
            /* A manual choice overrides automatic following. */
            g_followOs = FALSE;
            CheckMenuItem(g_menu, IDM_FOLLOWOS, MF_BYCOMMAND | MF_UNCHECKED);
            ApplyDark(!g_dark);
            SetStatus(g_dark ? "Dark mode on" : "Dark mode off");
            return 0;
        case IDM_FOLLOWOS:
            g_followOs = !g_followOs;
            CheckMenuItem(g_menu, IDM_FOLLOWOS,
                          MF_BYCOMMAND | (g_followOs ? MF_CHECKED : MF_UNCHECKED));
            if (g_followOs) {
                ApplyDark(ReadOsDark());
                SetStatus("Following OS dark mode");
            } else {
                SetStatus("No longer following OS dark mode");
            }
            return 0;
        }
        return 0;
    }

    case WM_SA_SAPI5EVENT:
        Sapi5_PumpEvents(g_engine);
        return 0;
    case WM_SA_OCPLAY:
        OneCore_DoPlay((char *)wp, (long)lp);
        return 0;
    case WM_SA_WORD:
        if (g_highlight) HighlightWord((int)wp, (int)lp);
        return 0;
    case WM_SA_STARTED:
        g_speaking = TRUE; g_paused = FALSE;
        SetStatus("Speaking");
        return 0;
    case WM_SA_DONE:
        g_speaking = FALSE; g_paused = FALSE;
        SetStatus("Ready");
        return 0;

    case WM_SA_WEBDONE: {
        char *text = (char *)wp;
        g_webBusy = FALSE;
        if (text) {
            SetWindowTextA(g_text, text);
            Mem_Free(text);
            SetStatus("Web page loaded - press F5 to speak");
            SetFocus(g_text);
        } else {
            SetStatus("Could not load the web page");
            MessageBoxA(g_main,
                "Could not load that web page.\n\n"
                "Check the address and your internet connection.",
                APP_TITLE, MB_OK | MB_ICONERROR);
        }
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        SaveSettings();
        Engines_ShutdownAll();
        if (g_font)  DeleteObject(g_font);
        if (g_brBg)  DeleteObject(g_brBg);
        if (g_brCtl) DeleteObject(g_brCtl);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

/* ---- entry ----------------------------------------------------------- */

int SpeakaliveMain(void)
{
    WNDCLASSEXA wc;
    INITCOMMONCONTROLSEX icc;
    HACCEL accel;
    MSG msg;

    g_inst = GetModuleHandleA(NULL);

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_BAR_CLASSES | ICC_TAB_CLASSES;
    InitCommonControlsEx(&icc);

    Engines_Init();

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = g_inst;
    wc.hIcon         = LoadIconA(NULL, (LPCSTR)IDI_APPLICATION);
    wc.hCursor       = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = APP_CLASS;
    wc.hIconSm       = wc.hIcon;
    RegisterClassExA(&wc);

    g_menu = LoadMenuA(g_inst, MAKEINTRESOURCEA(IDR_MENU));

    g_main = CreateWindowExA(0, APP_CLASS, APP_TITLE,
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, 660, 540,
                             NULL, g_menu, g_inst, NULL);
    if (!g_main) return 1;

    /* Restore the saved size/position, else show at the default size. */
    if (!RestoreWindowPlacement(g_main))
        ShowWindow(g_main, SW_SHOWNORMAL);
    UpdateWindow(g_main);

    accel = LoadAcceleratorsA(g_inst, MAKEINTRESOURCEA(IDA_ACCEL));

    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        if (accel && TranslateAcceleratorA(g_main, accel, &msg)) continue;
        if (IsDialogMessageA(g_main, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    CoUninitialize();
    return (int)msg.wParam;
}
