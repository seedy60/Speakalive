/* sapi5.c - Microsoft Speech API 5 back-end for Speakalive.
 *
 * SAPI 5 is the primary engine: present on Windows XP and later (and on
 * Windows 2000 if the SAPI 5.1 redistributable is installed).  It speaks
 * asynchronously so the UI stays responsive, supports native pause/resume/
 * stop, exposes rate and volume directly, applies pitch through an XML
 * <pitch> tag, and renders to a WAV file for the "save audio" feature.
 */
#include <windows.h>
#include <mmreg.h>
#include <sapi.h>   /* COBJMACROS is defined on the compiler command line */

#include "engine.h"
#include "util.h"
#include "audiofile.h"

typedef struct {
    ISpVoice *voice;
    Voice    *list;
    int       count;
    int       rate;     /* -10..10 */
    int       pitch;    /* -10..10 */
    int       vol;      /* 0..100  */
    int       speaking;
    int       prefixLen;/* wchars of injected <pitch> markup, for offset fix */
    HWND      notify;
} Sapi5;

static Sapi5 g_s5;

/* ---- helpers ---------------------------------------------------------- */

static void FreeVoices(Sapi5 *s)
{
    int i;
    if (s->list) {
        for (i = 0; i < s->count; i++) {
            ISpObjectToken *t = (ISpObjectToken *)s->list[i].data;
            if (t) ISpObjectToken_Release(t);
        }
        Mem_Free(s->list);
        s->list = NULL;
    }
    s->count = 0;
}

static void LoadVoices(Sapi5 *s)
{
    ISpObjectTokenCategory *cat = NULL;
    IEnumSpObjectTokens    *en  = NULL;
    ULONG count = 0, i;
    HRESULT hr;

    FreeVoices(s);

    hr = CoCreateInstance(&CLSID_SpObjectTokenCategory, NULL, CLSCTX_ALL,
                          &IID_ISpObjectTokenCategory, (void **)&cat);
    if (FAILED(hr)) return;

    hr = ISpObjectTokenCategory_SetId(cat, SPCAT_VOICES, FALSE);
    if (FAILED(hr)) { ISpObjectTokenCategory_Release(cat); return; }

    hr = ISpObjectTokenCategory_EnumTokens(cat, NULL, NULL, &en);
    if (SUCCEEDED(hr) && en) {
        IEnumSpObjectTokens_GetCount(en, &count);
        if (count) {
            s->list = (Voice *)Mem_Alloc(sizeof(Voice) * count);
            if (s->list) {
                for (i = 0; i < count; i++) {
                    ISpObjectToken *tok = NULL;
                    ULONG got = 0;
                    if (IEnumSpObjectTokens_Next(en, 1, &tok, &got) == S_OK && got == 1) {
                        WCHAR *desc = NULL;
                        char  *a;
                        int    idx = s->count;
                        s->list[idx].data  = tok;   /* keep the reference */
                        s->list[idx].index = idx;
                        if (SUCCEEDED(ISpObjectToken_GetStringValue(tok, NULL, &desc)) && desc) {
                            a = WideToAnsi(desc);
                            if (a) {
                                lstrcpynA(s->list[idx].name, a, MAX_VOICE_NAME);
                                Mem_Free(a);
                            }
                            CoTaskMemFree(desc);
                        }
                        if (s->list[idx].name[0] == 0)
                            lstrcpynA(s->list[idx].name, "Unnamed voice", MAX_VOICE_NAME);
                        s->count++;
                    }
                }
            }
        }
        IEnumSpObjectTokens_Release(en);
    }
    ISpObjectTokenCategory_Release(cat);
}

/* Build the wide string that is actually handed to SAPI, including the
 * pitch tag.  Stores the markup prefix length for word-offset correction. */
static WCHAR *BuildSpeakString(Sapi5 *s, const char *text, BOOL asXml)
{
    char  prefix[48];
    char *body, *full;
    WCHAR *wide;
    int   plen, blen;

    prefix[0] = 0;
    if (s->pitch != 0) {
        char num[16];
        lstrcpyA(prefix, "<pitch absmiddle=\"");
        IntToStr(s->pitch, num, sizeof(num));
        lstrcatA(prefix, num);
        lstrcatA(prefix, "\"/>");
    }
    s->prefixLen = lstrlenA(prefix); /* ASCII -> wchar count is identical */

    body = asXml ? StrDupA(text) : XmlEscapeA(text);
    if (!body) return NULL;

    plen = lstrlenA(prefix);
    blen = lstrlenA(body);
    full = (char *)Mem_Alloc((SIZE_T)plen + blen + 1);
    if (!full) { Mem_Free(body); return NULL; }
    if (plen) memcpy(full, prefix, plen);
    memcpy(full + plen, body, blen + 1);
    Mem_Free(body);

    wide = AnsiToWide(full);
    Mem_Free(full);
    return wide;
}

static void ApplyParams(Sapi5 *s)
{
    if (!s->voice) return;
    ISpVoice_SetRate(s->voice, (long)s->rate);
    ISpVoice_SetVolume(s->voice, (USHORT)s->vol);
}

/* ---- vtable implementation ------------------------------------------- */

static BOOL S5_Detect(void)
{
    /* Cheap presence test: can we create the SpVoice coclass? */
    ISpVoice *v = NULL;
    HRESULT   hr;
    BOOL      ok;
    hr = CoCreateInstance(&CLSID_SpVoice, NULL, CLSCTX_ALL,
                          &IID_ISpVoice, (void **)&v);
    ok = SUCCEEDED(hr) && v != NULL;
    if (v) ISpVoice_Release(v);
    return ok;
}

static BOOL S5_Init(SpeechEngine *e)
{
    Sapi5 *s = &g_s5;
    HRESULT hr;
    e->priv = s;
    if (s->voice) return TRUE; /* already initialised */

    hr = CoCreateInstance(&CLSID_SpVoice, NULL, CLSCTX_ALL,
                          &IID_ISpVoice, (void **)&s->voice);
    if (FAILED(hr) || !s->voice) return FALSE;

    s->rate = 0; s->pitch = 0; s->vol = 100;
    ApplyParams(s);
    LoadVoices(s);

    ISpVoice_SetInterest(s->voice,
        SPFEI(SPEI_WORD_BOUNDARY) | SPFEI(SPEI_END_INPUT_STREAM) |
        SPFEI(SPEI_START_INPUT_STREAM),
        SPFEI(SPEI_WORD_BOUNDARY) | SPFEI(SPEI_END_INPUT_STREAM) |
        SPFEI(SPEI_START_INPUT_STREAM));
    return TRUE;
}

static void S5_Shutdown(SpeechEngine *e)
{
    Sapi5 *s = (Sapi5 *)e->priv;
    if (!s) return;
    if (s->voice) {
        ISpVoice_Speak(s->voice, NULL, SPF_PURGEBEFORESPEAK, NULL);
        ISpVoice_Release(s->voice);
        s->voice = NULL;
    }
    FreeVoices(s);
}

static int S5_GetVoices(SpeechEngine *e, Voice **out)
{
    Sapi5 *s = (Sapi5 *)e->priv;
    *out = s->list;
    return s->count;
}

static BOOL S5_SetVoice(SpeechEngine *e, int index)
{
    Sapi5 *s = (Sapi5 *)e->priv;
    if (index < 0 || index >= s->count) return FALSE;
    return SUCCEEDED(ISpVoice_SetVoice(s->voice,
                     (ISpObjectToken *)s->list[index].data));
}

static void S5_SetRate(SpeechEngine *e, int rate)
{
    Sapi5 *s = (Sapi5 *)e->priv;
    s->rate = rate;
    if (s->voice) ISpVoice_SetRate(s->voice, (long)rate);
}

static void S5_SetPitch(SpeechEngine *e, int pitch)
{
    ((Sapi5 *)e->priv)->pitch = pitch;   /* applied at Speak time via XML */
}

static void S5_SetVolume(SpeechEngine *e, int vol)
{
    Sapi5 *s = (Sapi5 *)e->priv;
    s->vol = vol;
    if (s->voice) ISpVoice_SetVolume(s->voice, (USHORT)vol);
}

static BOOL S5_Speak(SpeechEngine *e, const char *text, BOOL asXml, HWND notify)
{
    Sapi5 *s = (Sapi5 *)e->priv;
    WCHAR *wide;
    HRESULT hr;

    if (!s->voice) return FALSE;
    s->notify = notify;
    if (notify)
        ISpVoice_SetNotifyWindowMessage(s->voice, notify, WM_SA_SAPI5EVENT, 0, 0);

    ApplyParams(s);
    wide = BuildSpeakString(s, text, asXml);
    if (!wide) return FALSE;

    s->speaking = 1;
    hr = ISpVoice_Speak(s->voice, wide,
                        SPF_ASYNC | SPF_IS_XML | SPF_PURGEBEFORESPEAK, NULL);
    Mem_Free(wide);
    if (FAILED(hr)) { s->speaking = 0; return FALSE; }
    return TRUE;
}

static BOOL S5_Pause(SpeechEngine *e)
{
    Sapi5 *s = (Sapi5 *)e->priv;
    return s->voice ? SUCCEEDED(ISpVoice_Pause(s->voice)) : FALSE;
}

static BOOL S5_Resume(SpeechEngine *e)
{
    Sapi5 *s = (Sapi5 *)e->priv;
    return s->voice ? SUCCEEDED(ISpVoice_Resume(s->voice)) : FALSE;
}

static BOOL S5_Stop(SpeechEngine *e)
{
    Sapi5 *s = (Sapi5 *)e->priv;
    if (!s->voice) return FALSE;
    /* Resume first: a paused voice will not flush its queue otherwise. */
    ISpVoice_Resume(s->voice);
    s->speaking = 0;
    return SUCCEEDED(ISpVoice_Speak(s->voice, NULL, SPF_PURGEBEFORESPEAK, NULL));
}

static BOOL S5_IsSpeaking(SpeechEngine *e)
{
    return ((Sapi5 *)e->priv)->speaking;
}

/* Render to the given format.  WAV is written directly; MP3 is produced by
 * first rendering a temp WAV then transcoding it through the ACM. */
static BOOL S5_Save(SpeechEngine *e, const char *text, BOOL asXml,
                    const char *path, int fmt, int channels)
{
    Sapi5 *s = (Sapi5 *)e->priv;
    ISpStream   *stream = NULL;
    WAVEFORMATEX wfx;
    WCHAR       *wpath, *wide;
    char        *wavPath;
    const char  *renderPath;
    HRESULT      hr;
    BOOL         ok = FALSE;

    if (!s->voice) return FALSE;
    if (channels != 2) channels = 1;

    renderPath = path;
    wavPath = NULL;
    if (fmt == FMT_MP3) {
        wavPath = AudioFile_TempWav();
        if (!wavPath) return FALSE;
        renderPath = wavPath;
    }

    ZeroMemory(&wfx, sizeof(wfx));
    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nChannels       = (WORD)channels;
    wfx.nSamplesPerSec  = 22050;
    wfx.wBitsPerSample  = 16;
    wfx.nBlockAlign     = (WORD)(channels * 2);
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize          = 0;

    wpath = AnsiToWide(renderPath);
    if (!wpath) { Mem_Free(wavPath); return FALSE; }

    hr = CoCreateInstance(&CLSID_SpStream, NULL, CLSCTX_ALL,
                          &IID_ISpStream, (void **)&stream);
    if (SUCCEEDED(hr)) {
        hr = ISpStream_BindToFile(stream, wpath, SPFM_CREATE_ALWAYS,
                                  &SPDFID_WaveFormatEx, &wfx, 0);
        if (SUCCEEDED(hr)) {
            ISpVoice_SetOutput(s->voice, (IUnknown *)stream, TRUE);
            ApplyParams(s);
            wide = BuildSpeakString(s, text, asXml);
            if (wide) {
                hr = ISpVoice_Speak(s->voice, wide, SPF_IS_XML, NULL); /* sync */
                Mem_Free(wide);
                ok = SUCCEEDED(hr);
            }
            ISpVoice_SetOutput(s->voice, NULL, TRUE); /* back to speakers */
        }
        ISpStream_Close(stream);
        ISpStream_Release(stream);
    }
    Mem_Free(wpath);

    if (ok && fmt == FMT_MP3)
        ok = AudioFile_WavToMp3(wavPath, path);
    if (wavPath) {
        DeleteFileA(wavPath);
        Mem_Free(wavPath);
    }
    return ok;
}

/* Called by the UI when WM_SA_SAPI5EVENT arrives: drain queued events and
 * translate word boundaries into highlight notifications. */
void Sapi5_PumpEvents(SpeechEngine *e)
{
    Sapi5  *s = (Sapi5 *)(e ? e->priv : &g_s5);
    SPEVENT ev;
    ULONG   fetched;
    if (!s || !s->voice) return;

    while (ISpVoice_GetEvents(s->voice, 1, &ev, &fetched) == S_OK && fetched == 1) {
        switch (ev.eEventId) {
            case SPEI_START_INPUT_STREAM:
                if (s->notify) PostMessageA(s->notify, WM_SA_STARTED, 0, 0);
                break;
            case SPEI_WORD_BOUNDARY: {
                int start = (int)ev.lParam - s->prefixLen;
                int len   = (int)ev.wParam;
                if (start < 0) { len += start; start = 0; }
                if (len < 0) len = 0;
                if (s->notify)
                    PostMessageA(s->notify, WM_SA_WORD, (WPARAM)start, (LPARAM)len);
                break;
            }
            case SPEI_END_INPUT_STREAM:
                s->speaking = 0;
                if (s->notify) PostMessageA(s->notify, WM_SA_DONE, 0, 0);
                break;
            default:
                break;
        }
        /* Free any object/string/pointer carried in lParam (sphelper's
         * SpClearEvent is C++ only, so do it by hand). */
        switch (ev.elParamType) {
            case SPET_LPARAM_IS_POINTER:
            case SPET_LPARAM_IS_STRING:
                if (ev.lParam) CoTaskMemFree((void *)ev.lParam);
                break;
            case SPET_LPARAM_IS_TOKEN:
            case SPET_LPARAM_IS_OBJECT:
                if (ev.lParam) IUnknown_Release((IUnknown *)ev.lParam);
                break;
            default:
                break;
        }
    }
}

static SpeechEngine g_engine = {
    "sapi5", "SAPI 5",
    TRUE,  /* pitch  */
    TRUE,  /* volume */
    S5_Detect, S5_Init, S5_Shutdown,
    S5_GetVoices, S5_SetVoice,
    S5_SetRate, S5_SetPitch, S5_SetVolume,
    S5_Speak, S5_Pause, S5_Resume, S5_Stop, S5_IsSpeaking,
    S5_Save,
    NULL
};

SpeechEngine *Sapi5_Get(void)
{
    g_engine.priv = &g_s5;
    return &g_engine;
}
