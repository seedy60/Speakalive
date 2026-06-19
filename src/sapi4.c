/* sapi4.c - Microsoft Speech API 4 back-end for Speakalive.
 *
 * Built against the genuine SAPI 4 SDK header (speech.h), so the COM vtable
 * layout is exactly correct rather than reconstructed.  SAPI 4 is the engine
 * that reaches the oldest systems (Windows 2000, and Windows 9x-era engines).
 *
 * Flow: CLSID_TTSEnumerator -> ITTSEnumW (enumerate voice "modes") ->
 * Select(modeID) gives an ITTSCentralW bound to an MMAudioDest (speakers).
 * Speech parameters come from ITTSAttributesW; SAPI 4 control tags in the
 * text are honoured via TTSDATAFLAG_TAGGED.
 *
 * NOTE: this engine could not be exercised on the Windows 11 build machine
 * (no SAPI 4 runtime present there).  It is detection-gated, so it is inert
 * unless a real SAPI 4 engine is installed.  Validate on SAPI 4 hardware.
 */
#include <windows.h>
#include "speech.h"

#include "engine.h"
#include "util.h"
#include "audiofile.h"

/* GUIDs taken verbatim from speech.h, kept file-local so they never collide
 * with anything in uuid.lib. */
static const GUID GUID_TTSEnumerator =
    {0xd67c0280,0xc743,0x11cd,{0x80,0xe5,0x00,0xaa,0x00,0x3e,0x4b,0x50}};
static const GUID GUID_MMAudioDest =
    {0xcb96b400,0xc743,0x11cd,{0x80,0xe5,0x00,0xaa,0x00,0x3e,0x4b,0x50}};
static const GUID GUID_ITTSEnumW =
    {0x6b837b20,0x4a47,0x101b,{0x93,0x1a,0x00,0xaa,0x00,0x47,0xba,0x4f}};
static const GUID GUID_ITTSAttributesW =
    {0x1287a280,0x4a47,0x101b,{0x93,0x1a,0x00,0xaa,0x00,0x47,0xba,0x4f}};
static const GUID GUID_IAudioMMDevice =
    {0xb68ad320,0xc743,0x11cd,{0x80,0xe5,0x00,0xaa,0x00,0x3e,0x4b,0x50}};
/* TextData wants the notify-sink interface IID even when the sink is NULL;
 * passing a null GUID returns E_INVALIDARG. */
static const GUID GUID_ITTSBufNotifySink =
    {0xe4963d40,0xc743,0x11cd,{0x80,0xe5,0x00,0xaa,0x00,0x3e,0x4b,0x50}};
/* File audio destination (CLSID_AudioDestFile / IAudioFile) - lets the engine
 * render straight to a .wav, and ITTSNotifySink tells us when it has finished. */
static const GUID GUID_AudioDestFile =
    {0xd4623720,0xe4b9,0x11cf,{0x8d,0x56,0x00,0xa0,0xc9,0x03,0x4a,0x7e}};
static const GUID GUID_IAudioFile =
    {0xfd7c2320,0x3d6d,0x11b9,{0xc0,0x00,0xfe,0xd6,0xcb,0xa3,0xb1,0xa9}};
static const GUID GUID_ITTSNotifySinkW =
    {0xc0fa8f40,0x4a46,0x101b,{0x93,0x1a,0x00,0xaa,0x00,0x47,0xba,0x4f}};
static const GUID GUID_IUnknownLocal =
    {0x00000000,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};

/* ---- ITTSNotifySink callback object (file-render completion) --------- */

static volatile LONG g_audioStopped;

static HRESULT STDMETHODCALLTYPE Sink_QI(ITTSNotifySinkW *This, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, &GUID_IUnknownLocal) || IsEqualIID(riid, &GUID_ITTSNotifySinkW)) {
        *ppv = This; return S_OK;
    }
    *ppv = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE Sink_AddRef(ITTSNotifySinkW *This)  { (void)This; return 1; }
static ULONG STDMETHODCALLTYPE Sink_Release(ITTSNotifySinkW *This) { (void)This; return 1; }
static HRESULT STDMETHODCALLTYPE Sink_AttribChanged(ITTSNotifySinkW *This, DWORD d)
{ (void)This; (void)d; return S_OK; }
static HRESULT STDMETHODCALLTYPE Sink_AudioStart(ITTSNotifySinkW *This, QWORD q)
{ (void)This; (void)q; return S_OK; }
static HRESULT STDMETHODCALLTYPE Sink_AudioStop(ITTSNotifySinkW *This, QWORD q)
{ (void)This; (void)q; InterlockedExchange(&g_audioStopped, 1); return S_OK; }
static HRESULT STDMETHODCALLTYPE Sink_Visual(ITTSNotifySinkW *This, QWORD q,
        WCHAR a, WCHAR b, DWORD d, PTTSMOUTH m)
{ (void)This; (void)q; (void)a; (void)b; (void)d; (void)m; return S_OK; }

static struct ITTSNotifySinkWVtbl g_sinkVtbl = {
    Sink_QI, Sink_AddRef, Sink_Release,
    Sink_AttribChanged, Sink_AudioStart, Sink_AudioStop, Sink_Visual
};
static ITTSNotifySinkW g_sink = { &g_sinkVtbl };

static BOOL ReadFileAll(const char *path, BYTE **outBuf, DWORD *outLen)
{
    HANDLE h;
    DWORD  sz, got;
    BYTE  *b;
    *outBuf = NULL; *outLen = 0;
    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    sz = GetFileSize(h, NULL);
    if (sz == INVALID_FILE_SIZE || sz == 0) { CloseHandle(h); return FALSE; }
    b = (BYTE *)Mem_Alloc(sz);
    if (!b) { CloseHandle(h); return FALSE; }
    if (!ReadFile(h, b, sz, &got, NULL) || got != sz) { CloseHandle(h); Mem_Free(b); return FALSE; }
    CloseHandle(h);
    *outBuf = b; *outLen = sz;
    return TRUE;
}

#define S4_MAXVOICES 64

typedef struct {
    ITTSEnumW              *enumr;
    ITTSCentralW           *central;
    ITTSAttributesW        *attr;
    IAudioMultiMediaDevice *audio;
    GUID    modes[S4_MAXVOICES];
    Voice   list[S4_MAXVOICES];
    int     count;
    DWORD   defSpeed;
    WORD    defPitch;
    int     rate;     /* -10..10 */
    int     pitch;    /* -10..10 */
    int     speaking;
    int     curVoice; /* index currently selected */
    WCHAR  *lastBuf;  /* kept alive while the async engine reads it */
} Sapi4;

static Sapi4 g_s4;

static void ApplyParamsTo(Sapi4 *s, ITTSAttributesW *a)
{
    if (!a) return;
    {
        int base = (int)s->defSpeed, step;
        if (base <= 0) base = 150;
        step = base / 10; if (step < 1) step = 1;
        base = base + s->rate * step;
        if (base < 1) base = 1;
        a->lpVtbl->SpeedSet(a, (DWORD)base);
    }
    {
        int base = (int)s->defPitch;
        if (base <= 0) base = 100;
        base = base + s->pitch * 5;
        if (base < 1) base = 1;
        if (base > 0xFFFF) base = 0xFFFF;
        a->lpVtbl->PitchSet(a, (WORD)base);
    }
}

static void ApplyParams(Sapi4 *s) { ApplyParamsTo(s, s->attr); }

static void ReleaseSelection(Sapi4 *s)
{
    if (s->attr)    { s->attr->lpVtbl->Release(s->attr);       s->attr = NULL; }
    if (s->central) { s->central->lpVtbl->Release(s->central); s->central = NULL; }
    if (s->audio)   { s->audio->lpVtbl->Release(s->audio);     s->audio = NULL; }
}

static BOOL DoSelect(Sapi4 *s, int index)
{
    HRESULT hr;
    if (index < 0 || index >= s->count || !s->enumr) return FALSE;
    ReleaseSelection(s);

    /* A multimedia (speaker) audio destination.  If it cannot be created we
     * still try Select with a NULL destination. */
    CoCreateInstance(&GUID_MMAudioDest, NULL, CLSCTX_ALL,
                     &GUID_IAudioMMDevice, (void **)&s->audio);

    hr = s->enumr->lpVtbl->Select(s->enumr, s->modes[index], &s->central,
                                  (LPUNKNOWN)s->audio);
    if (FAILED(hr) || !s->central) { ReleaseSelection(s); return FALSE; }
    s->curVoice = index;

    if (SUCCEEDED(s->central->lpVtbl->QueryInterface(s->central,
                  &GUID_ITTSAttributesW, (void **)&s->attr)) && s->attr) {
        if (FAILED(s->attr->lpVtbl->SpeedGet(s->attr, &s->defSpeed))) s->defSpeed = 150;
        if (FAILED(s->attr->lpVtbl->PitchGet(s->attr, &s->defPitch))) s->defPitch = 100;
        s->attr->lpVtbl->VolumeSet(s->attr, 0xFFFFFFFF); /* full volume */
    }
    ApplyParams(s);
    return TRUE;
}

/* ---- vtable ---------------------------------------------------------- */

static BOOL S4_Detect(void)
{
    ITTSEnumW *e = NULL;
    HRESULT hr = CoCreateInstance(&GUID_TTSEnumerator, NULL, CLSCTX_ALL,
                                  &GUID_ITTSEnumW, (void **)&e);
    if (SUCCEEDED(hr) && e) { e->lpVtbl->Release(e); return TRUE; }
    return FALSE;
}

static BOOL S4_Init(SpeechEngine *eng)
{
    Sapi4 *s = (Sapi4 *)eng->priv;
    HRESULT hr;
    if (s->enumr) return TRUE;

    hr = CoCreateInstance(&GUID_TTSEnumerator, NULL, CLSCTX_ALL,
                          &GUID_ITTSEnumW, (void **)&s->enumr);
    if (FAILED(hr) || !s->enumr) return FALSE;

    s->enumr->lpVtbl->Reset(s->enumr);
    for (;;) {
        TTSMODEINFOW mi;
        ULONG fetched = 0;
        char *a;
        if (s->enumr->lpVtbl->Next(s->enumr, 1, &mi, &fetched) != S_OK || fetched != 1)
            break;
        s->modes[s->count] = mi.gModeID;
        s->list[s->count].index = s->count;
        a = WideToAnsi(mi.szModeName[0] ? mi.szModeName : mi.szProductName);
        if (a) { lstrcpynA(s->list[s->count].name, a, MAX_VOICE_NAME); Mem_Free(a); }
        if (s->list[s->count].name[0] == 0)
            lstrcpynA(s->list[s->count].name, "SAPI 4 voice", MAX_VOICE_NAME);
        s->count++;
        if (s->count >= S4_MAXVOICES) break;
    }
    if (s->count > 0) DoSelect(s, 0);
    return TRUE;
}

static void S4_Shutdown(SpeechEngine *eng)
{
    Sapi4 *s = (Sapi4 *)eng->priv;
    if (s->central) s->central->lpVtbl->AudioReset(s->central);
    ReleaseSelection(s);
    if (s->enumr) { s->enumr->lpVtbl->Release(s->enumr); s->enumr = NULL; }
    if (s->lastBuf) { Mem_Free(s->lastBuf); s->lastBuf = NULL; }
    s->count = 0;
}

static int  S4_GetVoices(SpeechEngine *eng, Voice **out)
{
    Sapi4 *s = (Sapi4 *)eng->priv;
    *out = s->list;
    return s->count;
}

static BOOL S4_SetVoice(SpeechEngine *eng, int index)
{
    return DoSelect((Sapi4 *)eng->priv, index);
}

static void S4_SetRate(SpeechEngine *eng, int rate)
{
    Sapi4 *s = (Sapi4 *)eng->priv;
    s->rate = rate; ApplyParams(s);
}

static void S4_SetPitch(SpeechEngine *eng, int pitch)
{
    Sapi4 *s = (Sapi4 *)eng->priv;
    s->pitch = pitch; ApplyParams(s);
}

static void S4_SetVolume(SpeechEngine *eng, int v) { (void)eng; (void)v; }

static BOOL S4_Speak(SpeechEngine *eng, const char *text, BOOL asXml, HWND notify)
{
    Sapi4 *s = (Sapi4 *)eng->priv;
    WCHAR *w;
    SDATA  d;
    HRESULT hr;
    (void)asXml; (void)notify;

    if (!s->central) return FALSE;
    w = AnsiToWide(text);
    if (!w) return FALSE;

    /* Stop anything in progress, then it is safe to free the old buffer. */
    s->central->lpVtbl->AudioReset(s->central);
    if (s->lastBuf) { Mem_Free(s->lastBuf); s->lastBuf = NULL; }

    d.pData  = w;
    d.dwSize = (DWORD)((lstrlenW(w) + 1) * sizeof(WCHAR));
    hr = s->central->lpVtbl->TextData(s->central, CHARSET_TEXT, TTSDATAFLAG_TAGGED,
                                      d, NULL, GUID_ITTSBufNotifySink);
    if (FAILED(hr)) { Mem_Free(w); return FALSE; }

    s->lastBuf = w;   /* keep alive until next Speak/Stop/Shutdown */
    s->speaking = 1;
    return TRUE;
}

static BOOL S4_Pause(SpeechEngine *eng)
{
    Sapi4 *s = (Sapi4 *)eng->priv;
    return s->central ? SUCCEEDED(s->central->lpVtbl->AudioPause(s->central)) : FALSE;
}

static BOOL S4_Resume(SpeechEngine *eng)
{
    Sapi4 *s = (Sapi4 *)eng->priv;
    return s->central ? SUCCEEDED(s->central->lpVtbl->AudioResume(s->central)) : FALSE;
}

static BOOL S4_Stop(SpeechEngine *eng)
{
    Sapi4 *s = (Sapi4 *)eng->priv;
    if (!s->central) return FALSE;
    s->speaking = 0;
    return SUCCEEDED(s->central->lpVtbl->AudioReset(s->central));
}

static BOOL S4_IsSpeaking(SpeechEngine *eng) { return ((Sapi4 *)eng->priv)->speaking; }

/* Render to a file via the built-in CLSID_AudioDestFile.  We always render a
 * temporary WAV (the engine writes its native mono PCM), then hand it to
 * audiofile.c which produces the requested format / channel count. */
static BOOL S4_Save(SpeechEngine *eng, const char *text, BOOL asXml,
                    const char *path, int fmt, int channels)
{
    Sapi4           *s = (Sapi4 *)eng->priv;
    IAudioFile      *pIAF = NULL;
    ITTSCentralW    *fileCentral = NULL;
    ITTSAttributesW *fileAttr = NULL;
    WCHAR           *wtext = NULL, *wtmp = NULL;
    char            *tmpWav = NULL;
    BYTE            *buf = NULL;
    DWORD            len = 0, regKey = 0;
    BOOL             ok = FALSE;
    SDATA            d;
    (void)asXml;

    if (!s->enumr || s->count <= 0) return FALSE;

    tmpWav = AudioFile_TempWav();
    if (!tmpWav) return FALSE;
    wtmp  = AnsiToWide(tmpWav);
    wtext = AnsiToWide(text);
    if (!wtmp || !wtext) goto done;

    if (FAILED(CoCreateInstance(&GUID_AudioDestFile, NULL, CLSCTX_ALL,
                                &GUID_IAudioFile, (void **)&pIAF)) || !pIAF)
        goto done;

    /* A separate Select binds a fresh central to the file destination,
     * leaving the live speaker central untouched. */
    if (FAILED(s->enumr->lpVtbl->Select(s->enumr, s->modes[s->curVoice],
                                        &fileCentral, (LPUNKNOWN)pIAF)) || !fileCentral)
        goto done;

    if (SUCCEEDED(fileCentral->lpVtbl->QueryInterface(fileCentral,
                  &GUID_ITTSAttributesW, (void **)&fileAttr)) && fileAttr)
        ApplyParamsTo(s, fileAttr);

    pIAF->lpVtbl->RealTimeSet(pIAF, 0x0800);   /* render up to 8x real time   */

    g_audioStopped = 0;
    fileCentral->lpVtbl->Register(fileCentral, (PVOID)&g_sink,
                                  GUID_ITTSNotifySinkW, &regKey);

    if (FAILED(pIAF->lpVtbl->Set(pIAF, wtmp, 1))) goto done;

    d.pData  = wtext;
    d.dwSize = (DWORD)((lstrlenW(wtext) + 1) * sizeof(WCHAR));
    if (FAILED(fileCentral->lpVtbl->TextData(fileCentral, CHARSET_TEXT,
              TTSDATAFLAG_TAGGED, d, NULL, GUID_ITTSBufNotifySink)))
        goto done;

    /* Wait for the render to finish.  The engine delivers AudioStop either
     * from its own thread or via this STA's message queue, so do both: pump
     * messages and poll the flag. */
    {
        DWORD start = GetTickCount();
        while (!g_audioStopped) {
            MSG msg;
            while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
            if (GetTickCount() - start > 120000) break;  /* 2 min safety */
            Sleep(5);
        }
    }

    pIAF->lpVtbl->Flush(pIAF);                  /* finalise the .wav           */
    if (regKey) fileCentral->lpVtbl->UnRegister(fileCentral, regKey);

    if (ReadFileAll(tmpWav, &buf, &len) && len > 0)
        ok = AudioFile_WavBytesToFile(buf, len, path, fmt, channels);

done:
    Mem_Free(buf);
    if (fileAttr)    fileAttr->lpVtbl->Release(fileAttr);
    if (fileCentral) fileCentral->lpVtbl->Release(fileCentral);
    if (pIAF)        pIAF->lpVtbl->Release(pIAF);
    if (wtext) Mem_Free(wtext);
    if (wtmp)  Mem_Free(wtmp);
    if (tmpWav) { DeleteFileA(tmpWav); Mem_Free(tmpWav); }
    return ok;
}

static SpeechEngine g_engine = {
    "sapi4", "SAPI 4",
    TRUE,   /* pitch  */
    FALSE,  /* volume (SAPI 5 only per spec) */
    S4_Detect, S4_Init, S4_Shutdown,
    S4_GetVoices, S4_SetVoice,
    S4_SetRate, S4_SetPitch, S4_SetVolume,
    S4_Speak, S4_Pause, S4_Resume, S4_Stop, S4_IsSpeaking,
    S4_Save,
    NULL
};

SpeechEngine *Sapi4_Get(void)
{
    g_engine.priv = &g_s4;
    return &g_engine;
}
