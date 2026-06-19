/* onecore.c - Windows OneCore (WinRT) speech back-end for Speakalive.
 *
 * Uses Windows.Media.SpeechSynthesis through the WinRT C ABI.  Everything from
 * combase.dll (the Ro and WindowsString APIs) and shcore.dll
 * (CreateStreamOverRandomAccessStream) is bound with GetProcAddress so the
 * executable keeps *no* load-time dependency on those DLLs - they simply do
 * not exist on Windows 2000, where Detect() therefore returns FALSE and the
 * OneCore tab is not shown.
 *
 * Flow: build SSML (rate/pitch via <prosody>) -> SynthesizeSsmlToStreamAsync
 * -> poll the async op -> read the resulting WAV stream into memory -> play it
 * through MCI (which gives free pause/resume/stop) or hand it to audiofile.c
 * for "save as".
 */
#include <windows.h>
#include <mmsystem.h>
#include <windows.media.speechsynthesis.h>

#include "engine.h"
#include "util.h"
#include "audiofile.h"

/* Short aliases for the very long generated C ABI type names. */
typedef __x_ABI_CWindows_CMedia_CSpeechSynthesis_CISpeechSynthesizer       ISynth;
typedef __x_ABI_CWindows_CMedia_CSpeechSynthesis_CIInstalledVoicesStatic   IVoicesStatic;
typedef __x_ABI_CWindows_CMedia_CSpeechSynthesis_CIVoiceInformation        IVoiceInfo;
typedef __x_ABI_CWindows_CMedia_CSpeechSynthesis_CISpeechSynthesisStream   ISynthStream;
typedef __FIVectorView_1_Windows__CMedia__CSpeechSynthesis__CVoiceInformation     IVoiceVector;
typedef __FIAsyncOperation_1_Windows__CMedia__CSpeechSynthesis__CSpeechSynthesisStream IAsyncSynthOp;
/* IAsyncInfo is a classic MIDL interface (asyncinfo.h) - use it directly. */

/* IIDs not provided by uuid.lib - file-local so they never collide. */
static const GUID GUID_ISpeechSynthesizer =
    {0xce9f7c76,0x97f4,0x4ced,{0xad,0x68,0xd5,0x1c,0x45,0x8e,0x45,0xc6}};
static const GUID GUID_IInstalledVoicesStatic =
    {0x7d526ecc,0x7533,0x4c3f,{0x85,0xbe,0x88,0x8c,0x2b,0xae,0xeb,0xdc}};
static const GUID GUID_IAsyncInfo =
    {0x00000036,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};

/* Dynamically bound entry points. */
typedef HRESULT (WINAPI *PFN_RoActivateInstance)(HSTRING, IInspectable **);
typedef HRESULT (WINAPI *PFN_RoGetActivationFactory)(HSTRING, REFIID, void **);
typedef HRESULT (WINAPI *PFN_RoInitialize)(int);
typedef HRESULT (WINAPI *PFN_WindowsCreateString)(const WCHAR *, UINT32, HSTRING *);
typedef HRESULT (WINAPI *PFN_WindowsDeleteString)(HSTRING);
typedef const WCHAR * (WINAPI *PFN_WindowsGetStringRawBuffer)(HSTRING, UINT32 *);
typedef HRESULT (WINAPI *PFN_CreateStreamOverRAS)(IUnknown *, REFIID, void **);

#define MCI_ALIAS "SpeakaliveOC"

typedef struct {
    HMODULE combase, shcore;
    PFN_RoActivateInstance       RoActivate;
    PFN_RoGetActivationFactory   RoGetFactory;
    PFN_WindowsCreateString      WStrCreate;
    PFN_WindowsDeleteString      WStrDelete;
    PFN_WindowsGetStringRawBuffer WStrRaw;
    PFN_CreateStreamOverRAS      StreamOver;
    ISynth  *synth;
    Voice   *list;
    int      count;
    int      rate;     /* -10..10 */
    int      pitch;    /* -10..10 */
    int      speaking;
    int      opened;
    volatile LONG playGen;
    char    *tempWav;
    HWND     notify;
} OneCore;

static OneCore g_oc;

/* ---- helpers --------------------------------------------------------- */

static HSTRING MakeHS(OneCore *oc, const WCHAR *s)
{
    HSTRING h = NULL;
    if (oc->WStrCreate && s) oc->WStrCreate(s, (UINT32)lstrlenW(s), &h);
    return h;
}

static BOOL BindEntryPoints(OneCore *oc)
{
    if (oc->RoActivate) return TRUE; /* already bound */
    oc->combase = LoadLibraryA("combase.dll");
    oc->shcore  = LoadLibraryA("shcore.dll");
    if (!oc->combase || !oc->shcore) return FALSE;

    oc->RoActivate   = (PFN_RoActivateInstance)      GetProcAddress(oc->combase, "RoActivateInstance");
    oc->RoGetFactory = (PFN_RoGetActivationFactory)  GetProcAddress(oc->combase, "RoGetActivationFactory");
    oc->WStrCreate   = (PFN_WindowsCreateString)     GetProcAddress(oc->combase, "WindowsCreateString");
    oc->WStrDelete   = (PFN_WindowsDeleteString)     GetProcAddress(oc->combase, "WindowsDeleteString");
    oc->WStrRaw      = (PFN_WindowsGetStringRawBuffer)GetProcAddress(oc->combase, "WindowsGetStringRawBuffer");
    oc->StreamOver   = (PFN_CreateStreamOverRAS)     GetProcAddress(oc->shcore, "CreateStreamOverRandomAccessStream");

    {
        PFN_RoInitialize roInit = (PFN_RoInitialize)GetProcAddress(oc->combase, "RoInitialize");
        if (roInit) roInit(0); /* RO_INIT_SINGLETHREADED; ignore result */
    }
    return oc->RoActivate && oc->RoGetFactory && oc->WStrCreate &&
           oc->WStrDelete && oc->WStrRaw && oc->StreamOver;
}

static BOOL ActivateSynth(OneCore *oc)
{
    HSTRING cls;
    IInspectable *insp = NULL;
    HRESULT hr;
    if (oc->synth) return TRUE;
    cls = MakeHS(oc, RuntimeClass_Windows_Media_SpeechSynthesis_SpeechSynthesizer);
    if (!cls) return FALSE;
    hr = oc->RoActivate(cls, &insp);
    oc->WStrDelete(cls);
    if (FAILED(hr) || !insp) return FALSE;
    hr = insp->lpVtbl->QueryInterface(insp, &GUID_ISpeechSynthesizer, (void **)&oc->synth);
    insp->lpVtbl->Release(insp);
    return SUCCEEDED(hr) && oc->synth != NULL;
}

static void LoadVoices(OneCore *oc)
{
    HSTRING cls;
    IVoicesStatic *stat = NULL;
    IVoiceVector  *vec = NULL;
    UINT32 size = 0, i;
    HRESULT hr;

    if (oc->list) return;
    cls = MakeHS(oc, RuntimeClass_Windows_Media_SpeechSynthesis_SpeechSynthesizer);
    if (!cls) return;
    hr = oc->RoGetFactory(cls, &GUID_IInstalledVoicesStatic, (void **)&stat);
    oc->WStrDelete(cls);
    if (FAILED(hr) || !stat) return;

    if (SUCCEEDED(stat->lpVtbl->get_AllVoices(stat, &vec)) && vec) {
        vec->lpVtbl->get_Size(vec, &size);
        if (size) {
            oc->list = (Voice *)Mem_Alloc(sizeof(Voice) * size);
            if (oc->list) {
                for (i = 0; i < size; i++) {
                    IVoiceInfo *vi = NULL;
                    if (SUCCEEDED(vec->lpVtbl->GetAt(vec, i, &vi)) && vi) {
                        HSTRING name = NULL;
                        int idx = oc->count;
                        oc->list[idx].data  = vi;   /* keep the reference */
                        oc->list[idx].index = idx;
                        if (SUCCEEDED(vi->lpVtbl->get_DisplayName(vi, &name)) && name) {
                            const WCHAR *raw = oc->WStrRaw(name, NULL);
                            char *a = WideToAnsi(raw ? raw : L"");
                            if (a) { lstrcpynA(oc->list[idx].name, a, MAX_VOICE_NAME); Mem_Free(a); }
                            oc->WStrDelete(name);
                        }
                        if (oc->list[idx].name[0] == 0)
                            lstrcpynA(oc->list[idx].name, "Voice", MAX_VOICE_NAME);
                        oc->count++;
                    }
                }
            }
        }
        vec->lpVtbl->Release(vec);
    }
    stat->lpVtbl->Release(stat);
}

static char *BuildSsml(OneCore *oc, const char *text, BOOL asXml)
{
    char *esc, *out;
    char rbuf[16], pbuf[20];
    int  ratePct, pitchPct, len;
    const char *a = "<speak version='1.0' xmlns='http://www.w3.org/2001/10/synthesis' xml:lang='en-US'><prosody rate='";
    const char *b = "%' pitch='";
    const char *c = "%'>";
    const char *d = "</prosody></speak>";

    if (asXml) return StrDupA(text);   /* user supplied full SSML */

    esc = XmlEscapeA(text);
    if (!esc) return NULL;

    ratePct = 100 + oc->rate * 10;
    if (ratePct < 20) ratePct = 20;
    pitchPct = oc->pitch * 5;
    IntToStr(ratePct, rbuf, sizeof(rbuf));
    if (pitchPct >= 0) { pbuf[0] = '+'; IntToStr(pitchPct, pbuf + 1, sizeof(pbuf) - 1); }
    else IntToStr(pitchPct, pbuf, sizeof(pbuf));

    len = lstrlenA(a) + lstrlenA(rbuf) + lstrlenA(b) + lstrlenA(pbuf) +
          lstrlenA(c) + lstrlenA(esc) + lstrlenA(d) + 1;
    out = (char *)Mem_Alloc(len);
    if (out) {
        lstrcpyA(out, a); lstrcatA(out, rbuf); lstrcatA(out, b); lstrcatA(out, pbuf);
        lstrcatA(out, c); lstrcatA(out, esc);  lstrcatA(out, d);
    }
    Mem_Free(esc);
    return out;
}

/* Synthesise to an in-memory WAV image.  Caller frees *outBuf. */
static BOOL Synthesize(OneCore *oc, const char *text, BOOL asXml,
                       BYTE **outBuf, DWORD *outLen)
{
    char         *ssml;
    WCHAR        *wssml;
    HSTRING       hs;
    IAsyncSynthOp *op = NULL;
    IAsyncInfo   *info = NULL;
    ISynthStream *strm = NULL;
    IStream      *ps = NULL;
    BYTE         *buf, *chunk;
    DWORD         cap = 65536, used = 0;
    HRESULT       hr;

    *outBuf = NULL; *outLen = 0;
    if (!oc->synth) return FALSE;

    ssml = BuildSsml(oc, text, asXml);
    if (!ssml) return FALSE;
    wssml = AnsiToWide(ssml);
    Mem_Free(ssml);
    if (!wssml) return FALSE;
    hs = MakeHS(oc, wssml);
    Mem_Free(wssml);
    if (!hs) return FALSE;

    hr = oc->synth->lpVtbl->SynthesizeSsmlToStreamAsync(oc->synth, hs, &op);
    oc->WStrDelete(hs);
    if (FAILED(hr) || !op) return FALSE;

    /* Poll the async operation to completion (status 1 = Completed). */
    if (SUCCEEDED(op->lpVtbl->QueryInterface(op, &GUID_IAsyncInfo, (void **)&info)) && info) {
        for (;;) {
            AsyncStatus st = (AsyncStatus)0;
            info->lpVtbl->get_Status(info, &st);
            if (st == 1) break;            /* Completed */
            if (st >= 2) {                 /* Canceled / Error */
                info->lpVtbl->Release(info);
                op->lpVtbl->Release(op);
                return FALSE;
            }
            Sleep(5);
        }
        info->lpVtbl->Release(info);
    }

    hr = op->lpVtbl->GetResults(op, &strm);
    op->lpVtbl->Release(op);
    if (FAILED(hr) || !strm) return FALSE;

    hr = oc->StreamOver((IUnknown *)strm, &IID_IStream, (void **)&ps);
    strm->lpVtbl->Release(strm);
    if (FAILED(hr) || !ps) return FALSE;

    buf   = (BYTE *)Mem_Alloc(cap);
    chunk = (BYTE *)Mem_Alloc(65536);
    if (!buf || !chunk) { ps->lpVtbl->Release(ps); Mem_Free(buf); Mem_Free(chunk); return FALSE; }
    for (;;) {
        ULONG got = 0;
        HRESULT r = ps->lpVtbl->Read(ps, chunk, 65536, &got);
        if (got == 0 || FAILED(r)) break;
        if (used + got > cap) {
            while (used + got > cap) cap *= 2;
            buf = (BYTE *)Mem_ReAlloc(buf, cap);
            if (!buf) { Mem_Free(chunk); ps->lpVtbl->Release(ps); return FALSE; }
        }
        memcpy(buf + used, chunk, got);
        used += got;
    }
    Mem_Free(chunk);
    ps->lpVtbl->Release(ps);

    *outBuf = buf; *outLen = used;
    return used > 0;
}

/* ---- MCI playback ---------------------------------------------------- */

static void StopPlayback(OneCore *oc)
{
    InterlockedIncrement(&oc->playGen); /* supersede any running poll thread */
    if (oc->opened) {
        mciSendStringA("stop " MCI_ALIAS, NULL, 0, NULL);
        mciSendStringA("close " MCI_ALIAS, NULL, 0, NULL);
        oc->opened = 0;
    }
    oc->speaking = 0;
}

static DWORD WINAPI PollThread(LPVOID param)
{
    LONG myGen = (LONG)(INT_PTR)param;
    char mode[32];
    for (;;) {
        Sleep(150);
        if (g_oc.playGen != myGen) return 0;       /* superseded */
        mode[0] = 0;
        if (mciSendStringA("status " MCI_ALIAS " mode", mode, sizeof(mode), NULL) != 0)
            break;
        if (lstrcmpiA(mode, "stopped") == 0) break; /* finished naturally */
    }
    if (g_oc.playGen == myGen) {
        g_oc.speaking = 0;
        if (g_oc.notify) PostMessageA(g_oc.notify, WM_SA_DONE, 0, 0);
    }
    return 0;
}

/* ---- vtable ---------------------------------------------------------- */

static BOOL OC_Detect(void)
{
    OneCore *oc = &g_oc;
    if (!BindEntryPoints(oc)) return FALSE;
    return ActivateSynth(oc);
}

static BOOL OC_Init(SpeechEngine *e)
{
    OneCore *oc = (OneCore *)e->priv;
    if (!oc->synth && !ActivateSynth(oc)) return FALSE;
    LoadVoices(oc);
    return TRUE;
}

static void OC_Shutdown(SpeechEngine *e)
{
    OneCore *oc = (OneCore *)e->priv;
    int i;
    StopPlayback(oc);
    if (oc->tempWav) { DeleteFileA(oc->tempWav); Mem_Free(oc->tempWav); oc->tempWav = NULL; }
    if (oc->list) {
        for (i = 0; i < oc->count; i++) {
            IVoiceInfo *vi = (IVoiceInfo *)oc->list[i].data;
            if (vi) vi->lpVtbl->Release(vi);
        }
        Mem_Free(oc->list);
        oc->list = NULL;
    }
    oc->count = 0;
    if (oc->synth) { oc->synth->lpVtbl->Release(oc->synth); oc->synth = NULL; }
}

static int OC_GetVoices(SpeechEngine *e, Voice **out)
{
    OneCore *oc = (OneCore *)e->priv;
    *out = oc->list;
    return oc->count;
}

static BOOL OC_SetVoice(SpeechEngine *e, int index)
{
    OneCore *oc = (OneCore *)e->priv;
    if (index < 0 || index >= oc->count || !oc->synth) return FALSE;
    return SUCCEEDED(oc->synth->lpVtbl->put_Voice(oc->synth,
                     (IVoiceInfo *)oc->list[index].data));
}

static void OC_SetRate(SpeechEngine *e, int rate)   { ((OneCore *)e->priv)->rate = rate; }
static void OC_SetPitch(SpeechEngine *e, int pitch) { ((OneCore *)e->priv)->pitch = pitch; }
static void OC_SetVolume(SpeechEngine *e, int v)    { (void)e; (void)v; }

static BOOL OC_Speak(SpeechEngine *e, const char *text, BOOL asXml, HWND notify)
{
    OneCore *oc = (OneCore *)e->priv;
    BYTE  *buf = NULL;
    DWORD  len = 0, w;
    HANDLE h;
    char   cmd[MAX_PATH + 64];
    LONG   gen;

    oc->notify = notify;
    StopPlayback(oc);

    if (!Synthesize(oc, text, asXml, &buf, &len)) return FALSE;

    if (oc->tempWav) { DeleteFileA(oc->tempWav); Mem_Free(oc->tempWav); oc->tempWav = NULL; }
    oc->tempWav = AudioFile_TempWav();
    if (!oc->tempWav) { Mem_Free(buf); return FALSE; }

    h = CreateFileA(oc->tempWav, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { Mem_Free(buf); return FALSE; }
    WriteFile(h, buf, len, &w, NULL);
    CloseHandle(h);
    Mem_Free(buf);

    lstrcpyA(cmd, "open \"");
    lstrcatA(cmd, oc->tempWav);
    lstrcatA(cmd, "\" type waveaudio alias " MCI_ALIAS);
    if (mciSendStringA(cmd, NULL, 0, NULL) != 0) return FALSE;
    oc->opened = 1;

    gen = InterlockedIncrement(&oc->playGen);
    oc->speaking = 1;
    if (mciSendStringA("play " MCI_ALIAS, NULL, 0, NULL) != 0) { StopPlayback(oc); return FALSE; }

    {
        HANDLE th = CreateThread(NULL, 0, PollThread, (LPVOID)(INT_PTR)gen, 0, NULL);
        if (th) CloseHandle(th);
    }
    return TRUE;
}

static BOOL OC_Pause(SpeechEngine *e)
{
    OneCore *oc = (OneCore *)e->priv;
    if (!oc->opened) return FALSE;
    return mciSendStringA("pause " MCI_ALIAS, NULL, 0, NULL) == 0;
}

static BOOL OC_Resume(SpeechEngine *e)
{
    OneCore *oc = (OneCore *)e->priv;
    if (!oc->opened) return FALSE;
    return mciSendStringA("resume " MCI_ALIAS, NULL, 0, NULL) == 0;
}

static BOOL OC_Stop(SpeechEngine *e)
{
    StopPlayback((OneCore *)e->priv);
    return TRUE;
}

static BOOL OC_IsSpeaking(SpeechEngine *e) { return ((OneCore *)e->priv)->speaking; }

static BOOL OC_Save(SpeechEngine *e, const char *text, BOOL asXml,
                    const char *path, int fmt, int channels)
{
    OneCore *oc = (OneCore *)e->priv;
    BYTE  *buf = NULL;
    DWORD  len = 0;
    BOOL   ok;
    if (!Synthesize(oc, text, asXml, &buf, &len)) return FALSE;
    ok = AudioFile_WavBytesToFile(buf, len, path, fmt, channels);
    Mem_Free(buf);
    return ok;
}

static SpeechEngine g_engine = {
    "onecore", "OneCore",
    TRUE,   /* pitch (via SSML) */
    FALSE,  /* volume (SAPI 5 only per spec) */
    OC_Detect, OC_Init, OC_Shutdown,
    OC_GetVoices, OC_SetVoice,
    OC_SetRate, OC_SetPitch, OC_SetVolume,
    OC_Speak, OC_Pause, OC_Resume, OC_Stop, OC_IsSpeaking,
    OC_Save,
    NULL
};

SpeechEngine *OneCore_Get(void)
{
    g_engine.priv = &g_oc;
    return &g_engine;
}
