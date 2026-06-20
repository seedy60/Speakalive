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
    int     curVoice;   /* index currently selected */
    /* Live (F5) chunked speaking: the text is fed to the engine one chunk at a
     * time, the next queued when the previous finishes (SAPI 4 chokes on one
     * huge buffer). */
    WCHAR  *speakText;  /* full text being spoken (heap, owned)            */
    WCHAR  *speakChunk; /* reusable per-chunk buffer                       */
    int     speakPos;   /* next char offset into speakText                 */
    int     speakTotal;
    HWND    speakNotify;
    long    speakGen;   /* bumped on Stop / new Speak to drop stale callbacks */
    DWORD   speakRegKey;
    int     speakSinkReg;
} Sapi4;

static Sapi4 g_s4;

/* --- chunking shared by live speech and the file render --- */
#define S4_CHUNK_CHARS 4000

/* Length (in WCHARs) of the chunk at w[start]: at most 'max', broken at the
 * last sentence end (or space) so words are never split. */
static int S4_ChunkLen(const WCHAR *w, int start, int total, int max)
{
    int i, lastBreak = -1, lastSpace = -1;
    if (total - start <= max) return total - start;
    for (i = 0; i < max; i++) {
        WCHAR c = w[start + i];
        if (c == L'.' || c == L'!' || c == L'?' || c == L'\n') lastBreak = i;
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') lastSpace = i;
    }
    if (lastBreak >= 0) return lastBreak + 1;
    if (lastSpace >= 0) return lastSpace + 1;
    return max;
}

/* Notify sink for live speech: each finished chunk posts WM_SA_S4NEXT so the
 * UI thread can feed the next one (SAPI 4 objects have thread affinity, so the
 * feed must happen on the thread that created the central). */
static HRESULT STDMETHODCALLTYPE Sink_AudioStop_Speak(ITTSNotifySinkW *This, QWORD q)
{
    (void)This; (void)q;
    if (g_s4.speakNotify)
        PostMessageA(g_s4.speakNotify, WM_SA_S4NEXT, (WPARAM)g_s4.speakGen, 0);
    return S_OK;
}
static struct ITTSNotifySinkWVtbl g_speakSinkVtbl = {
    Sink_QI, Sink_AddRef, Sink_Release,
    Sink_AttribChanged, Sink_AudioStart, Sink_AudioStop_Speak, Sink_Visual
};
static ITTSNotifySinkW g_speakSink = { &g_speakSinkVtbl };

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
    if (s->speakSinkReg && s->central) {
        s->central->lpVtbl->UnRegister(s->central, s->speakRegKey);
        s->speakSinkReg = 0;
    }
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
    /* Listen for chunk completions so we can queue the next chunk. */
    if (SUCCEEDED(s->central->lpVtbl->Register(s->central, (PVOID)&g_speakSink,
                  GUID_ITTSNotifySinkW, &s->speakRegKey)))
        s->speakSinkReg = 1;
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
    s->speaking = 0;
    s->speakGen++;
    ReleaseSelection(s);
    if (s->enumr) { s->enumr->lpVtbl->Release(s->enumr); s->enumr = NULL; }
    if (s->speakText)  { Mem_Free(s->speakText);  s->speakText = NULL; }
    if (s->speakChunk) { Mem_Free(s->speakChunk); s->speakChunk = NULL; }
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

/* Feed the chunk at speakPos (or finish if there is none left).  Runs on the
 * UI thread - from S4_Speak for the first chunk and from Sapi4_SpeakNextChunk
 * (via WM_SA_S4NEXT) for the rest. */
static void S4_FeedChunk(Sapi4 *s)
{
    int   clen;
    SDATA d;

    if (!s->central || !s->speakText) return;

    if (s->speakPos >= s->speakTotal) {            /* whole text spoken */
        s->speaking = 0;
        Mem_Free(s->speakText); s->speakText = NULL;
        if (s->speakNotify) PostMessageA(s->speakNotify, WM_SA_DONE, 0, 0);
        return;
    }

    clen = S4_ChunkLen(s->speakText, s->speakPos, s->speakTotal, S4_CHUNK_CHARS);
    memcpy(s->speakChunk, s->speakText + s->speakPos, (SIZE_T)clen * sizeof(WCHAR));
    s->speakChunk[clen] = 0;
    s->speakPos += clen;

    d.pData  = s->speakChunk;
    d.dwSize = (DWORD)((clen + 1) * sizeof(WCHAR));
    if (FAILED(s->central->lpVtbl->TextData(s->central, CHARSET_TEXT,
              TTSDATAFLAG_TAGGED, d, NULL, GUID_ITTSBufNotifySink))) {
        s->speaking = 0;
        Mem_Free(s->speakText); s->speakText = NULL;
        if (s->speakNotify) PostMessageA(s->speakNotify, WM_SA_DONE, 0, 0);
    }
}

static BOOL S4_Speak(SpeechEngine *eng, const char *text, BOOL asXml, HWND notify)
{
    Sapi4 *s = (Sapi4 *)eng->priv;
    WCHAR *w;
    (void)asXml;

    if (!s->central) return FALSE;
    w = AnsiToWide(text);
    if (!w) return FALSE;
    if (!s->speakChunk) {
        s->speakChunk = (WCHAR *)Mem_Alloc((S4_CHUNK_CHARS + 1) * sizeof(WCHAR));
        if (!s->speakChunk) { Mem_Free(w); return FALSE; }
    }

    s->central->lpVtbl->AudioReset(s->central);  /* stop anything in progress */
    s->speakGen++;                               /* drop stale chunk callbacks */
    if (s->speakText) Mem_Free(s->speakText);
    s->speakText   = w;
    s->speakPos    = 0;
    s->speakTotal  = lstrlenW(w);
    s->speakNotify = notify;
    s->speaking    = 1;
    S4_FeedChunk(s);                             /* kick off the first chunk  */
    return TRUE;
}

/* Called by the UI (WM_SA_S4NEXT) when a chunk finishes; queues the next one.
 * 'gen' rejects callbacks left over from a Stop or a replaced utterance. */
void Sapi4_SpeakNextChunk(long gen)
{
    Sapi4 *s = &g_s4;
    if (s->speaking && gen == s->speakGen) S4_FeedChunk(s);
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
    s->speakGen++;                               /* drop pending chunk callbacks */
    if (s->speakText) { Mem_Free(s->speakText); s->speakText = NULL; }
    return SUCCEEDED(s->central->lpVtbl->AudioReset(s->central));
}

static BOOL S4_IsSpeaking(SpeechEngine *eng) { return ((Sapi4 *)eng->priv)->speaking; }

/* SAPI 4 (the old L&H engine) lags badly or fails outright when handed a very
 * long buffer in a single TextData call, so the file render also feeds it in
 * bite-sized chunks (S4_ChunkLen, above) and stitches the PCM together. */

/* Render one (null-terminated) chunk to 'wtmp'.  FALSE on error or cancel. */
static BOOL S4_RenderChunk(Sapi4 *s, const WCHAR *wchunk, DWORD byteSize,
                           WCHAR *wtmp)
{
    IAudioFile      *pIAF = NULL;
    ITTSCentralW    *fileCentral = NULL;
    ITTSAttributesW *fileAttr = NULL;
    DWORD            regKey = 0;
    BOOL             ok = FALSE;
    SDATA            d;

    if (FAILED(CoCreateInstance(&GUID_AudioDestFile, NULL, CLSCTX_ALL,
                                &GUID_IAudioFile, (void **)&pIAF)) || !pIAF)
        goto done;
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

    d.pData  = (PVOID)wchunk;
    d.dwSize = byteSize;
    if (FAILED(fileCentral->lpVtbl->TextData(fileCentral, CHARSET_TEXT,
              TTSDATAFLAG_TAGGED, d, NULL, GUID_ITTSBufNotifySink)))
        goto done;

    /* Wait for this chunk to finish.  AudioStop arrives from the engine thread
     * or this STA's queue, so pump messages and poll the flag. */
    {
        DWORD start = GetTickCount();
        while (!g_audioStopped) {
            MSG msg;
            if (g_saveCancel) { fileCentral->lpVtbl->AudioReset(fileCentral); goto done; }
            while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
            if (GetTickCount() - start > 120000) break;  /* 2 min safety */
            Sleep(5);
        }
    }
    pIAF->lpVtbl->Flush(pIAF);                  /* finalise the chunk .wav     */
    ok = !g_saveCancel;

done:
    if (regKey && fileCentral) fileCentral->lpVtbl->UnRegister(fileCentral, regKey);
    if (fileAttr)    fileAttr->lpVtbl->Release(fileAttr);
    if (fileCentral) fileCentral->lpVtbl->Release(fileCentral);
    if (pIAF)        pIAF->lpVtbl->Release(pIAF);
    return ok;
}

static BOOL S4_Save(SpeechEngine *eng, const char *text, BOOL asXml,
                    const char *path, int fmt, int channels)
{
    Sapi4 *s = (Sapi4 *)eng->priv;
    WCHAR *wtext = NULL, *wtmp = NULL, *chunkBuf = NULL;
    char  *tmpWav = NULL;
    BYTE  *pcmAll = NULL;
    DWORD  pcmCap = 0, pcmLen = 0;
    WAVEFORMATEX wf;
    BOOL   haveFmt = FALSE, ok = FALSE;
    int    total, pos;
    (void)asXml;

    if (!s->enumr || s->count <= 0) return FALSE;

    wtext  = AnsiToWide(text);
    tmpWav = AudioFile_TempWav();
    if (!wtext || !tmpWav) goto done;
    wtmp     = AnsiToWide(tmpWav);
    chunkBuf = (WCHAR *)Mem_Alloc((S4_CHUNK_CHARS + 1) * sizeof(WCHAR));
    if (!wtmp || !chunkBuf) goto done;

    total = lstrlenW(wtext);

    for (pos = 0; pos < total; ) {
        int   clen = S4_ChunkLen(wtext, pos, total, S4_CHUNK_CHARS);
        BYTE *cpcm = NULL;
        DWORD cpcmLen = 0;
        WAVEFORMATEX cwf;

        memcpy(chunkBuf, wtext + pos, (SIZE_T)clen * sizeof(WCHAR));
        chunkBuf[clen] = 0;

        if (!S4_RenderChunk(s, chunkBuf, (DWORD)((clen + 1) * sizeof(WCHAR)), wtmp))
            goto done;     /* error or cancel */
        if (!AudioFile_ReadWavPcm(tmpWav, &cpcm, &cpcmLen, &cwf))
            goto done;
        if (!haveFmt) { wf = cwf; haveFmt = TRUE; }

        if (pcmLen + cpcmLen > pcmCap) {
            DWORD need = pcmLen + cpcmLen, ncap = pcmCap ? pcmCap : (1u << 20);
            while (ncap < need) ncap *= 2;
            pcmAll = (BYTE *)Mem_ReAlloc(pcmAll, ncap);
            if (!pcmAll) { Mem_Free(cpcm); goto done; }
            pcmCap = ncap;
        }
        memcpy(pcmAll + pcmLen, cpcm, cpcmLen);
        pcmLen += cpcmLen;
        Mem_Free(cpcm);

        pos += clen;
    }

    if (haveFmt && pcmLen > 0)
        ok = AudioFile_PcmToFile(pcmAll, pcmLen, &wf, path, fmt, channels);

done:
    Mem_Free(pcmAll);
    if (chunkBuf) Mem_Free(chunkBuf);
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
