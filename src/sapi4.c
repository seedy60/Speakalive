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
 * NOTE: the build machine now has SAPI 4 runtimes (Microsoft Sam, L&H voices),
 * so this engine can be exercised here directly - the tools/test_s4*.c console
 * harnesses drive it (render speed, and the concurrency-safety check that proved
 * the L&H engine must be rendered serially).  It stays detection-gated, so it is
 * inert on systems with no SAPI 4 engine installed.
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

/* ---- ITTSNotifySink callbacks (shared by the live-speak and file sinks) - */

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
static HRESULT STDMETHODCALLTYPE Sink_Visual(ITTSNotifySinkW *This, QWORD q,
        WCHAR a, WCHAR b, DWORD d, PTTSMOUTH m)
{ (void)This; (void)q; (void)a; (void)b; (void)d; (void)m; return S_OK; }

#define S4_MAXVOICES 64

typedef struct {
    ITTSEnumW              *enumr;
    ITTSCentralW           *central;
    ITTSAttributesW        *attr;
    IAudioMultiMediaDevice *audio;
    GUID    modes[S4_MAXVOICES];
    DWORD   modeFeat[S4_MAXVOICES]; /* TTSFEATURE_* bits per voice (dwFeatures) */
    Voice   list[S4_MAXVOICES];
    int     count;
    DWORD   features;             /* dwFeatures of the selected voice */
    DWORD   defSpeed;
    WORD    defPitch;
    DWORD   speedMin, speedMax;   /* the engine's real accepted speed range */
    WORD    pitchMin, pitchMax;   /* the engine's real accepted pitch range */
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

/* ---- SAPI 4 teardown crash guard --------------------------------------------
 * Some third-party voice DLLs ("Carlos", "Willi", ...) fault inside their own
 * code when the audio destination they were bound to is released.  The dest's
 * scarce device resource is freed only by that very release (it cannot be reused
 * - other voices then crash on Select - nor leaked - it runs out after ~50), so
 * we release it normally but SURVIVE a fault: S4_RelSave saves a recovery point;
 * on an access violation while a guard is active on this thread, S4_CrashFilter
 * restores it and resumes just past the release, abandoning that one broken
 * object.  Win2000-safe (SetUnhandledExceptionFilter + CONTINUE_EXECUTION).
 *
 * g_s4Step records the current step (no I/O) so a genuinely unguarded crash
 * still logs the faulting voice/step to speakalive_crash.log.  [diagnostic] */
static char g_s4Step[256];
static void S4_Mark(int idx, DWORD feat, const char *name, const char *step)
{
    wsprintfA(g_s4Step, "voice[%d] feat=0x%08lx \"%s\" -> %s",
              idx, (unsigned long)feat, name ? name : "?", step ? step : "?");
}

static DWORD         g_relJb[6];   /* ebx, esi, edi, ebp, esp(post-ret), eip */
static volatile LONG g_relGuard;   /* a guarded teardown is in progress      */
static DWORD         g_relThread;  /* the thread that owns the guard         */

/* setjmp-style: save the recovery point and return 0.  After a caught fault the
 * crash filter makes this appear to "return" 1. */
__declspec(naked) static int S4_RelSave(void)
{
    __asm {
        mov  [g_relJb + 0],  ebx
        mov  [g_relJb + 4],  esi
        mov  [g_relJb + 8],  edi
        mov  [g_relJb + 12], ebp
        lea  eax, [esp + 4]              ; esp as it will be after the ret
        mov  [g_relJb + 16], eax
        mov  eax, [esp]                 ; return address (resume point)
        mov  [g_relJb + 20], eax
        xor  eax, eax                   ; first call returns 0
        ret
    }
}

static LONG WINAPI S4_CrashFilter(EXCEPTION_POINTERS *ep)
{
    char path[MAX_PATH], line[512];
    DWORD n, w;
    HANDLE h;
    /* A fault inside a guarded teardown on our thread: resume past it. */
    if (ep && g_relGuard && GetCurrentThreadId() == g_relThread &&
        ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        CONTEXT *c = ep->ContextRecord;
        c->Ebx = g_relJb[0]; c->Esi = g_relJb[1]; c->Edi = g_relJb[2];
        c->Ebp = g_relJb[3]; c->Esp = g_relJb[4]; c->Eip = g_relJb[5];
        c->Eax = 1;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    /* Otherwise it is a genuine unguarded crash - log it and terminate. */
    n = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (n && n < MAX_PATH) {
        while (n > 0 && path[n - 1] != '\\') n--;
        lstrcpynA(path + n, "speakalive_crash.log", (int)(MAX_PATH - n));
        wsprintfA(line, "SAPI 4 crash: code=0x%08lx addr=0x%p\r\nlast step: %s\r\n",
                  (unsigned long)(ep ? ep->ExceptionRecord->ExceptionCode : 0),
                  ep ? ep->ExceptionRecord->ExceptionAddress : (PVOID)0, g_s4Step);
        h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            SetFilePointer(h, 0, NULL, FILE_END);
            WriteFile(h, line, lstrlenA(line), &w, NULL);
            FlushFileBuffers(h);
            CloseHandle(h);
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

/* SAPI 4 Set() REJECTS an out-of-range speed/pitch (it returns failure and
 * leaves the value unchanged) rather than clamping, and every voice has its own
 * range - e.g. L&H Peter accepts pitch up to 337 where Michelle stops at 190.
 * These probes step out from the default and stop at the first value Set
 * refuses, so the sliders can span each voice's true range.  Stepping (rather
 * than a binary search) is deliberate: it tests just ONE value past each edge,
 * never the wildly out-of-range values a binary search would, because some
 * fragile third-party voice DLLs crash on those instead of returning an error.
 * 'kind' 0 = speed, 1 = pitch. */
#define S4_PROBE_STEP 10
static BOOL S4_TrySet(ITTSAttributesW *a, int kind, DWORD v)
{
    return kind == 0 ? SUCCEEDED(a->lpVtbl->SpeedSet(a, v))
                     : SUCCEEDED(a->lpVtbl->PitchSet(a, (WORD)v));
}
static DWORD S4_ProbeHi(ITTSAttributesW *a, int kind, DWORD def, DWORD cap)
{
    DWORD v = def;
    while (v + S4_PROBE_STEP <= cap && S4_TrySet(a, kind, v + S4_PROBE_STEP))
        v += S4_PROBE_STEP;
    return v;
}
static DWORD S4_ProbeLo(ITTSAttributesW *a, int kind, DWORD def)
{
    DWORD v = def;
    while (v > S4_PROBE_STEP && S4_TrySet(a, kind, v - S4_PROBE_STEP))
        v -= S4_PROBE_STEP;
    return v;
}

/* Map the -10..10 sliders onto the voice's real range: 0 = default, +10 = the
 * engine's max, -10 = its min (linear on each side).  Falls back to the old
 * default-relative formula if a range has not been probed (speedMax stays 0). */
static void S4_MapParams(Sapi4 *s, DWORD *speed, WORD *pitch)
{
    DWORD dS = s->defSpeed ? s->defSpeed : 150;
    DWORD dP = s->defPitch ? s->defPitch : 100;
    long  v;

    /* A nonzero min means the range was probed (see DoSelect).  Each slider half
     * is handled independently, so a voice with headroom on only one side (its
     * min == default, say) still reaches its max on the other. */
    if (s->speedMin) {
        if (s->rate >= 0)
            v = s->speedMax > dS ? (long)dS + (long)(s->speedMax - dS) *  s->rate / 10 : (long)dS;
        else
            v = s->speedMin < dS ? (long)dS - (long)(dS - s->speedMin) * (-s->rate) / 10 : (long)dS;
    } else {
        long step = (long)dS / 10; if (step < 1) step = 1;
        v = (long)dS + (long)s->rate * step;
    }
    if (v < 1) v = 1;
    *speed = (DWORD)v;

    if (s->pitchMin) {
        if (s->pitch >= 0)
            v = s->pitchMax > dP ? (long)dP + (long)(s->pitchMax - dP) *  s->pitch / 10 : (long)dP;
        else
            v = s->pitchMin < dP ? (long)dP - (long)(dP - s->pitchMin) * (-s->pitch) / 10 : (long)dP;
    } else {
        v = (long)dP + (long)s->pitch * 5;
    }
    if (v < 1) v = 1; if (v > 0xFFFF) v = 0xFFFF;
    *pitch = (WORD)v;
}

static void ApplyParamsTo(Sapi4 *s, ITTSAttributesW *a)
{
    DWORD speed; WORD pitch;
    if (!a) return;
    S4_MapParams(s, &speed, &pitch);
    /* Only set what the voice supports - see the dwFeatures note in DoSelect. */
    if (s->features & TTSFEATURE_SPEED) a->lpVtbl->SpeedSet(a, speed);
    if (s->features & TTSFEATURE_PITCH) a->lpVtbl->PitchSet(a, pitch);
}

static void ApplyParams(Sapi4 *s) { ApplyParamsTo(s, s->attr); }

static void ReleaseSelection(Sapi4 *s)
{
    int ci = (s->curVoice >= 0 && s->curVoice < s->count) ? s->curVoice : -1;
    const char *cn = (ci >= 0) ? s->list[ci].name : "?";

    /* Release the engine objects under the teardown guard (see S4_RelSave): a
     * fault inside a fragile voice's cleanup is caught and that object abandoned
     * instead of crashing the program.  There is only one Sapi4 (g_s4), so the
     * recovery branch clears it directly rather than via a possibly-clobbered
     * register. */
    g_relThread = GetCurrentThreadId();
    if (S4_RelSave() == 0) {
        g_relGuard = 1;
        if (s->speakSinkReg && s->central) {
            S4_Mark(ci, s->features, cn, "release: unregister sink");
            s->central->lpVtbl->UnRegister(s->central, s->speakRegKey);
            s->speakSinkReg = 0;
        }
        if (s->attr)    { S4_Mark(ci, s->features, cn, "release: attr");    s->attr->lpVtbl->Release(s->attr);       s->attr = NULL; }
        if (s->central) { S4_Mark(ci, s->features, cn, "release: central"); s->central->lpVtbl->Release(s->central); s->central = NULL; }
        if (s->audio)   { S4_Mark(ci, s->features, cn, "release: audio");   s->audio->lpVtbl->Release(s->audio);     s->audio = NULL; }
        g_relGuard = 0;
        s->speedMin = s->speedMax = 0;   /* re-probed per voice in DoSelect */
        s->pitchMin = s->pitchMax = 0;
        s->features = 0;
    } else {
        /* A voice DLL faulted mid-teardown: abandon (leak) whatever is left so
         * the app survives.  Only a handful of broken voices ever hit this.
         * Use g_s4 (the sole instance) since 's' may be in a clobbered reg. */
        g_relGuard = 0;
        g_s4.attr = NULL; g_s4.central = NULL; g_s4.audio = NULL; g_s4.speakSinkReg = 0;
        g_s4.speedMin = g_s4.speedMax = 0;
        g_s4.pitchMin = g_s4.pitchMax = 0;
        g_s4.features = 0;
    }
}

static BOOL DoSelect(Sapi4 *s, int index)
{
    HRESULT hr;
    const char *nm;
    DWORD ft;
    if (index < 0 || index >= s->count || !s->enumr) return FALSE;
    nm = s->list[index].name;
    ft = s->modeFeat[index];
    ReleaseSelection(s);

    /* A fresh multimedia (speaker) audio destination for every selection - it
     * cannot be reused (a later voice Select()ed onto a used one crashes fragile
     * voices like "Deep Douglas").  ReleaseSelection (above) freed the previous
     * one under the teardown crash guard.  If it cannot be created we still try
     * Select with a NULL dest. */
    S4_Mark(index, ft, nm, "create audio dest");
    CoCreateInstance(&GUID_MMAudioDest, NULL, CLSCTX_ALL,
                     &GUID_IAudioMMDevice, (void **)&s->audio);

    S4_Mark(index, ft, nm, "Select");
    hr = s->enumr->lpVtbl->Select(s->enumr, s->modes[index], &s->central,
                                  (LPUNKNOWN)s->audio);
    if (FAILED(hr) || !s->central) { ReleaseSelection(s); return FALSE; }
    s->curVoice = index;

    s->features = ft;
    s->defSpeed = 150;   /* sane fallbacks if the voice lacks speed/pitch */
    s->defPitch = 100;

    S4_Mark(index, ft, nm, "QI attributes");
    if (SUCCEEDED(s->central->lpVtbl->QueryInterface(s->central,
                  &GUID_ITTSAttributesW, (void **)&s->attr)) && s->attr) {
        /* Only ever touch attributes the voice advertises in dwFeatures:
         * calling SpeedSet/PitchSet (or probing the range) on a voice that does
         * not support them crashes some fragile third-party SAPI 4 voice DLLs.
         * The probe itself is gentle (see S4_ProbeHi) for the same reason. */
        if (s->features & TTSFEATURE_SPEED) {
            S4_Mark(index, ft, nm, "speed get + probe");
            if (FAILED(s->attr->lpVtbl->SpeedGet(s->attr, &s->defSpeed))) s->defSpeed = 150;
            s->speedMax = S4_ProbeHi(s->attr, 0, s->defSpeed, 1000);
            s->speedMin = S4_ProbeLo(s->attr, 0, s->defSpeed);
            s->attr->lpVtbl->SpeedSet(s->attr, s->defSpeed);   /* restore default */
        }
        if (s->features & TTSFEATURE_PITCH) {
            S4_Mark(index, ft, nm, "pitch get + probe");
            if (FAILED(s->attr->lpVtbl->PitchGet(s->attr, &s->defPitch))) s->defPitch = 100;
            s->pitchMax = (WORD)S4_ProbeHi(s->attr, 1, s->defPitch, 1000);
            s->pitchMin = (WORD)S4_ProbeLo(s->attr, 1, s->defPitch);
            s->attr->lpVtbl->PitchSet(s->attr, s->defPitch);   /* restore default */
        }
        if (s->features & TTSFEATURE_VOLUME) {
            S4_Mark(index, ft, nm, "volume set");
            s->attr->lpVtbl->VolumeSet(s->attr, 0xFFFFFFFF);   /* full volume */
        }
    }
    /* Listen for chunk completions so we can queue the next chunk. */
    S4_Mark(index, ft, nm, "register sink");
    if (SUCCEEDED(s->central->lpVtbl->Register(s->central, (PVOID)&g_speakSink,
                  GUID_ITTSNotifySinkW, &s->speakRegKey)))
        s->speakSinkReg = 1;
    S4_Mark(index, ft, nm, "apply params");
    ApplyParams(s);
    S4_Mark(index, ft, nm, "done");
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

/* A-law / mu-law voices are named like "Ludoviko 8000 A" / "8000 U" (vs the
 * Linear "...L"): their 8 kHz G.711 telephony output cannot be played through
 * the speaker - the engine refuses the speaker destination, so they only ever
 * error on live speak.  Hidden from the voice list; the Linear (L) variants,
 * which are higher quality and do play, remain. */
static BOOL S4_IsTelephonyOnly(const char *name)
{
    int n = lstrlenA(name);
    return n >= 3 && name[n - 2] == ' ' &&
           (name[n - 1] == 'A' || name[n - 1] == 'U') &&
           name[n - 3] >= '0' && name[n - 3] <= '9';
}

static BOOL S4_Init(SpeechEngine *eng)
{
    Sapi4 *s = (Sapi4 *)eng->priv;
    HRESULT hr;
    if (s->enumr) return TRUE;

    SetUnhandledExceptionFilter(S4_CrashFilter);   /* TEMP: capture select crash */

    hr = CoCreateInstance(&GUID_TTSEnumerator, NULL, CLSCTX_ALL,
                          &GUID_ITTSEnumW, (void **)&s->enumr);
    if (FAILED(hr) || !s->enumr) return FALSE;

    s->enumr->lpVtbl->Reset(s->enumr);
    for (;;) {
        TTSMODEINFOW mi;
        ULONG fetched = 0;
        char *a;
        char nm[MAX_VOICE_NAME];
        if (s->enumr->lpVtbl->Next(s->enumr, 1, &mi, &fetched) != S_OK || fetched != 1)
            break;
        nm[0] = 0;
        a = WideToAnsi(mi.szModeName[0] ? mi.szModeName : mi.szProductName);
        if (a) { lstrcpynA(nm, a, MAX_VOICE_NAME); Mem_Free(a); }
        if (nm[0] == 0) lstrcpynA(nm, "SAPI 4 voice", MAX_VOICE_NAME);
        if (S4_IsTelephonyOnly(nm)) continue;   /* A-law/mu-law: not playable */
        s->modes[s->count] = mi.gModeID;
        s->modeFeat[s->count] = mi.dwFeatures;
        s->list[s->count].index = s->count;
        lstrcpynA(s->list[s->count].name, nm, MAX_VOICE_NAME);
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
 * long buffer in a single TextData call, so a long file save is fed to it in
 * bite-sized chunks (S4_ChunkLen, above) and the PCM stitched back together.
 * The chunks are rendered strictly one at a time: the L&H runtime is not
 * thread-safe and corrupts the audio if instances render concurrently (it does
 * run them concurrently without erroring - tools/test_s4par.c - but the output
 * is wrong: tools/test_s4safe.c).  Its shared state is per-process, so rendering
 * chunks in separate helper processes IS safe and fast (tools/test_s4proc.c),
 * but that was judged not worth the extra moving parts.  RealTimeSet(0xFFFF)
 * below is the real speed win for fast voices; genuinely slow voices stay
 * engine-bound, as in Balabolka. */

/* Per-render completion sink.  The COM object is the first member, so the
 * 'This' handed to the callbacks casts straight back to the S4Sink and each
 * render gets its own 'stopped' flag (clearer than a global, and ready should a
 * future SAPI 4 engine ever prove safe to parallelise). */
typedef struct { ITTSNotifySinkW sink; volatile LONG stopped; } S4Sink;
static HRESULT STDMETHODCALLTYPE Sink_AudioStop_File(ITTSNotifySinkW *This, QWORD q)
{ (void)q; InterlockedExchange(&((S4Sink *)This)->stopped, 1); return S_OK; }
static struct ITTSNotifySinkWVtbl g_fileSinkVtbl = {
    Sink_QI, Sink_AddRef, Sink_Release,
    Sink_AttribChanged, Sink_AudioStart, Sink_AudioStop_File, Sink_Visual
};

/* Render one null-terminated chunk to 'wtmp' using the caller's enumerator.
 * Creates its own central, file destination and sink per call.  Called serially
 * (see S4_Save).  FALSE on error or cancel. */
static BOOL S4_RenderChunk(ITTSEnumW *en, GUID mode, DWORD feat,
                           DWORD speed, WORD pitch,
                           const WCHAR *chunk, const WCHAR *wtmp)
{
    IAudioFile      *pIAF = NULL;
    ITTSCentralW    *fc   = NULL;
    ITTSAttributesW *at   = NULL;
    DWORD            regKey = 0, start;
    BOOL             ok = FALSE;
    S4Sink           sink;
    SDATA            d;
    MSG              msg;

    sink.sink.lpVtbl = &g_fileSinkVtbl;
    sink.stopped     = 0;

    if (FAILED(CoCreateInstance(&GUID_AudioDestFile, NULL, CLSCTX_ALL,
                                &GUID_IAudioFile, (void **)&pIAF)) || !pIAF)
        return FALSE;
    if (FAILED(en->lpVtbl->Select(en, mode, &fc, (LPUNKNOWN)pIAF)) || !fc)
        goto done;
    if (SUCCEEDED(fc->lpVtbl->QueryInterface(fc, &GUID_ITTSAttributesW,
                  (void **)&at)) && at) {
        /* Only set what the voice advertises - see the dwFeatures note in DoSelect. */
        if (feat & TTSFEATURE_SPEED)  at->lpVtbl->SpeedSet(at, speed);
        if (feat & TTSFEATURE_PITCH)  at->lpVtbl->PitchSet(at, pitch);
        if (feat & TTSFEATURE_VOLUME) at->lpVtbl->VolumeSet(at, 0xFFFFFFFF);
    }
    /* wTime caps how far the engine may outrun real time when rendering to the
     * file: a real-time *multiple* (~value/256), NOT a buffer.  The old 0x0800
     * (8x) left Microsoft Sam at ~22s for a page; 0xFFFF (the maximum, "go as
     * fast as you can") drops it to ~5s with byte-identical audio; 0 means 0x
     * and hangs - never use it.  Measured with tools/test_s4speed.c. */
    pIAF->lpVtbl->RealTimeSet(pIAF, 0xFFFF);
    fc->lpVtbl->Register(fc, (PVOID)&sink.sink, GUID_ITTSNotifySinkW, &regKey);
    if (FAILED(pIAF->lpVtbl->Set(pIAF, (LPCWSTR)wtmp, 1))) goto done;

    d.pData  = (PVOID)chunk;
    d.dwSize = (DWORD)((lstrlenW(chunk) + 1) * sizeof(WCHAR));
    if (FAILED(fc->lpVtbl->TextData(fc, CHARSET_TEXT, TTSDATAFLAG_TAGGED,
              d, NULL, GUID_ITTSBufNotifySink)))
        goto done;

    /* AudioStop arrives via this thread's STA queue, so pump and poll. */
    start = GetTickCount();
    while (!sink.stopped) {
        if (g_saveCancel) { fc->lpVtbl->AudioReset(fc); goto done; }
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (GetTickCount() - start > 120000) break;   /* 2 min safety */
        Sleep(2);
    }
    pIAF->lpVtbl->Flush(pIAF);
    ok = !g_saveCancel;

done:
    if (regKey && fc) fc->lpVtbl->UnRegister(fc, regKey);
    if (at) at->lpVtbl->Release(at);
    if (fc) fc->lpVtbl->Release(fc);
    if (pIAF) pIAF->lpVtbl->Release(pIAF);
    return ok;
}

/* SAPI 4 file save.  The L&H runtime is NOT thread-safe: rendering chunks
 * concurrently corrupts the audio - separately verified with
 * tools/test_s4safe.c, where concurrent chunks came out different from the same
 * chunks rendered serially, some even empty.  So chunks are rendered one at a
 * time.  S4_CHUNK_CHARS is large, so a typical page is a single chunk with no
 * joins at all; only very long text is split, and only ever at sentence ends
 * (S4_ChunkLen) where the join lands in natural silence. */
static BOOL S4_Save(SpeechEngine *eng, const char *text, BOOL asXml,
                    const char *path, int fmt, int channels)
{
    Sapi4 *s = (Sapi4 *)eng->priv;
    WCHAR *wtext = NULL, *wtmp = NULL, *chunkBuf = NULL;
    char  *tmpWav = NULL;
    BYTE  *pcmAll = NULL;
    DWORD  pcmCap = 0, pcmLen = 0, speed = 0;
    WORD   pitch = 0;
    WAVEFORMATEX wf;
    BOOL   haveFmt = FALSE, ok = FALSE;
    int    total, pos;
    GUID   mode;
    (void)asXml;

    if (!s->enumr || s->count <= 0) return FALSE;

    wtext  = AnsiToWide(text);
    tmpWav = AudioFile_TempWav();
    if (!wtext || !tmpWav) goto done;
    wtmp     = AnsiToWide(tmpWav);
    chunkBuf = (WCHAR *)Mem_Alloc((S4_CHUNK_CHARS + 1) * sizeof(WCHAR));
    if (!wtmp || !chunkBuf) goto done;

    mode  = s->modes[s->curVoice];
    S4_MapParams(s, &speed, &pitch);
    total = lstrlenW(wtext);

    for (pos = 0; pos < total; ) {
        int   clen = S4_ChunkLen(wtext, pos, total, S4_CHUNK_CHARS);
        BYTE *cpcm = NULL;
        DWORD cpcmLen = 0;
        WAVEFORMATEX cwf;

        memcpy(chunkBuf, wtext + pos, (SIZE_T)clen * sizeof(WCHAR));
        chunkBuf[clen] = 0;

        if (!S4_RenderChunk(s->enumr, mode, s->features, speed, pitch, chunkBuf, wtmp))
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
