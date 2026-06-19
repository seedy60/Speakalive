/* audiofile.c - WAV helpers and MP3 transcoding for Speakalive.
 *
 * SAPI renders PCM WAV natively.  For MP3 we first render a temporary WAV and
 * then transcode it here, trying two strategies in order:
 *
 *   1. The Audio Compression Manager (acm) with an installed MP3 encoder.
 *      Stock Windows usually ships only an MP3 *decoder*, so this commonly
 *      fails - but if a Fraunhofer/LAME ACM encoder is present it is used.
 *   2. A bundled or PATH-resident lame.exe (lame.exe next to Speakalive.exe
 *      or anywhere on PATH).
 *
 * If neither is available the caller is told MP3 encoding is unavailable.
 */
#include <windows.h>
#include <mmreg.h>
#include <msacm.h>

#include "util.h"
#include "engine.h"
#include "audiofile.h"

/* ---- temp WAV path --------------------------------------------------- */

char *AudioFile_TempWav(void)
{
    char  dir[MAX_PATH];
    char  file[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, dir);
    if (n == 0 || n > MAX_PATH) lstrcpyA(dir, ".\\");
    if (GetTempFileNameA(dir, "sav", 0, file) == 0) return NULL;
    return StrDupA(file); /* a real (empty) file now exists at this path */
}

/* ---- minimal WAV reader ---------------------------------------------- */

typedef struct {
    BYTE        *file;     /* whole file image (heap)         */
    WAVEFORMATEX fmt;      /* parsed format                   */
    const BYTE  *data;     /* -> PCM samples within 'file'    */
    DWORD        dataLen;  /* bytes of PCM                    */
} WavData;

static DWORD rd32(const BYTE *p)
{
    return (DWORD)p[0] | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) | ((DWORD)p[3] << 24);
}

static BOOL WavRead(const char *path, WavData *w)
{
    HANDLE h;
    DWORD  size, got, pos;
    BYTE  *buf;

    ZeroMemory(w, sizeof(*w));
    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    size = GetFileSize(h, NULL);
    if (size == INVALID_FILE_SIZE || size < 44) { CloseHandle(h); return FALSE; }
    buf = (BYTE *)Mem_Alloc(size);
    if (!buf) { CloseHandle(h); return FALSE; }
    if (!ReadFile(h, buf, size, &got, NULL) || got != size) {
        CloseHandle(h); Mem_Free(buf); return FALSE;
    }
    CloseHandle(h);

    if (buf[0] != 'R' || buf[1] != 'I' || buf[2] != 'F' || buf[3] != 'F' ||
        buf[8] != 'W' || buf[9] != 'A' || buf[10] != 'V' || buf[11] != 'E') {
        Mem_Free(buf); return FALSE;
    }
    w->file = buf;

    pos = 12;
    while (pos + 8 <= size) {
        DWORD id  = rd32(buf + pos);
        DWORD len = rd32(buf + pos + 4);
        const BYTE *body = buf + pos + 8;
        if (pos + 8 + len > size) len = size - pos - 8;
        if (id == 0x20746d66) { /* "fmt " */
            DWORD copy = len < sizeof(WAVEFORMATEX) ? len : sizeof(WAVEFORMATEX);
            memcpy(&w->fmt, body, copy);
        } else if (id == 0x61746164) { /* "data" */
            w->data    = body;
            w->dataLen = len;
        }
        pos += 8 + len + (len & 1); /* chunks are word aligned */
    }
    if (!w->data || w->fmt.nChannels == 0) { Mem_Free(buf); w->file = NULL; return FALSE; }
    return TRUE;
}

static void WavFree(WavData *w)
{
    if (w->file) { Mem_Free(w->file); w->file = NULL; }
}

/* ---- ACM (encoder) path ---------------------------------------------- */

static BOOL EncodeAcm(const WavData *w, const char *mp3Path)
{
    MPEGLAYER3WAVEFORMAT mp3;
    HACMSTREAM   has = NULL;
    ACMSTREAMHEADER hdr;
    DWORD        dstLen = 0, bitrate = 128000;
    BYTE        *dst;
    HANDLE       h;
    DWORD        written;
    MMRESULT     mr;

    ZeroMemory(&mp3, sizeof(mp3));
    mp3.wfx.wFormatTag      = WAVE_FORMAT_MPEGLAYER3;
    mp3.wfx.nChannels       = w->fmt.nChannels;
    mp3.wfx.nSamplesPerSec  = w->fmt.nSamplesPerSec;
    mp3.wfx.wBitsPerSample  = 0;
    mp3.wfx.nBlockAlign      = 1;
    mp3.wfx.nAvgBytesPerSec = bitrate / 8;
    mp3.wfx.cbSize          = MPEGLAYER3_WFX_EXTRA_BYTES;
    mp3.wID                 = MPEGLAYER3_ID_MPEG;
    mp3.fdwFlags            = MPEGLAYER3_FLAG_PADDING_OFF;
    mp3.nBlockSize          = (WORD)(144 * bitrate / w->fmt.nSamplesPerSec);
    mp3.nFramesPerBlock     = 1;
    mp3.nCodecDelay         = 0;

    mr = acmStreamOpen(&has, NULL, (LPWAVEFORMATEX)&w->fmt,
                       (LPWAVEFORMATEX)&mp3, NULL, 0, 0, 0);
    if (mr != 0 || !has) return FALSE;

    if (acmStreamSize(has, w->dataLen, &dstLen, ACM_STREAMSIZEF_SOURCE) != 0 ||
        dstLen == 0) {
        acmStreamClose(has, 0); return FALSE;
    }
    dst = (BYTE *)Mem_Alloc(dstLen);
    if (!dst) { acmStreamClose(has, 0); return FALSE; }

    ZeroMemory(&hdr, sizeof(hdr));
    hdr.cbStruct     = sizeof(hdr);
    hdr.pbSrc        = (LPBYTE)w->data;
    hdr.cbSrcLength  = w->dataLen;
    hdr.pbDst        = dst;
    hdr.cbDstLength  = dstLen;

    if (acmStreamPrepareHeader(has, &hdr, 0) != 0) {
        Mem_Free(dst); acmStreamClose(has, 0); return FALSE;
    }
    mr = acmStreamConvert(has, &hdr, ACM_STREAMCONVERTF_BLOCKALIGN);
    if (mr != 0) {
        acmStreamUnprepareHeader(has, &hdr, 0);
        Mem_Free(dst); acmStreamClose(has, 0); return FALSE;
    }

    h = CreateFileA(mp3Path, GENERIC_WRITE, 0, NULL,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        WriteFile(h, dst, hdr.cbDstLengthUsed, &written, NULL);
        CloseHandle(h);
    }
    acmStreamUnprepareHeader(has, &hdr, 0);
    Mem_Free(dst);
    acmStreamClose(has, 0);
    return (h != INVALID_HANDLE_VALUE);
}

/* ---- lame.exe fallback ----------------------------------------------- */

static BOOL EncodeLame(const char *wavPath, const char *mp3Path)
{
    char  cmd[MAX_PATH * 3];
    char  exe[MAX_PATH];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    BOOL  ok = FALSE;
    const char *lame = "lame.exe";

    /* Prefer lame.exe sitting next to Speakalive.exe. */
    if (GetModuleFileNameA(NULL, exe, MAX_PATH)) {
        char *slash = exe;
        char *last = NULL, *p;
        for (p = exe; *p; p++) if (*p == '\\') last = p;
        if (last) {
            lstrcpyA(last + 1, "lame.exe");
            if (GetFileAttributesA(exe) != INVALID_FILE_ATTRIBUTES) lame = exe;
        }
        (void)slash;
    }

    lstrcpyA(cmd, "\"");
    lstrcatA(cmd, lame);
    lstrcatA(cmd, "\" --silent \"");
    lstrcatA(cmd, wavPath);
    lstrcatA(cmd, "\" \"");
    lstrcatA(cmd, mp3Path);
    lstrcatA(cmd, "\"");

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE,
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 60000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        if (GetFileAttributesA(mp3Path) != INVALID_FILE_ATTRIBUTES) ok = TRUE;
    }
    return ok;
}

BOOL AudioFile_WavToMp3(const char *wavPath, const char *mp3Path)
{
    WavData w;
    BOOL    ok;
    if (!WavRead(wavPath, &w)) return FALSE;
    ok = EncodeAcm(&w, mp3Path);
    WavFree(&w);
    if (!ok) ok = EncodeLame(wavPath, mp3Path);
    return ok;
}

/* ---- in-memory WAV -> file (with optional mono->stereo) -------------- */

static void wr32(BYTE *p, DWORD v)
{
    p[0] = (BYTE)v; p[1] = (BYTE)(v >> 8); p[2] = (BYTE)(v >> 16); p[3] = (BYTE)(v >> 24);
}
static void wr16(BYTE *p, WORD v) { p[0] = (BYTE)v; p[1] = (BYTE)(v >> 8); }

/* Parse a RIFF/WAVE held entirely in memory.  '*data' points inside 'buf'. */
static BOOL WavParseMem(const BYTE *buf, DWORD size, WAVEFORMATEX *fmt,
                        const BYTE **data, DWORD *dataLen)
{
    DWORD pos = 12;
    ZeroMemory(fmt, sizeof(*fmt));
    *data = NULL; *dataLen = 0;
    if (size < 44 || buf[0] != 'R' || buf[1] != 'I' || buf[2] != 'F' || buf[3] != 'F' ||
        buf[8] != 'W' || buf[9] != 'A' || buf[10] != 'V' || buf[11] != 'E')
        return FALSE;
    while (pos + 8 <= size) {
        DWORD id  = rd32(buf + pos);
        DWORD len = rd32(buf + pos + 4);
        if (pos + 8 + len > size) len = size - pos - 8;
        if (id == 0x20746d66) {
            DWORD copy = len < sizeof(WAVEFORMATEX) ? len : sizeof(WAVEFORMATEX);
            memcpy(fmt, buf + pos + 8, copy);
        } else if (id == 0x61746164) {
            *data = buf + pos + 8; *dataLen = len;
        }
        pos += 8 + len + (len & 1);
    }
    return (*data != NULL && fmt->nChannels != 0);
}

/* Build a canonical 16-byte-fmt RIFF/WAVE file from raw PCM. */
static BOOL WriteWavFile(const char *path, WORD channels, DWORD rate, WORD bits,
                         const BYTE *pcm, DWORD pcmLen)
{
    BYTE   hdr[44];
    HANDLE h;
    DWORD  w, blockAlign = channels * (bits / 8);
    h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    memcpy(hdr, "RIFF", 4);
    wr32(hdr + 4, 36 + pcmLen);
    memcpy(hdr + 8, "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    wr32(hdr + 16, 16);
    wr16(hdr + 20, 1);                 /* PCM */
    wr16(hdr + 22, channels);
    wr32(hdr + 24, rate);
    wr32(hdr + 28, rate * blockAlign); /* avg bytes/sec */
    wr16(hdr + 32, (WORD)blockAlign);
    wr16(hdr + 34, bits);
    memcpy(hdr + 36, "data", 4);
    wr32(hdr + 40, pcmLen);
    WriteFile(h, hdr, 44, &w, NULL);
    WriteFile(h, pcm, pcmLen, &w, NULL);
    CloseHandle(h);
    return TRUE;
}

BOOL AudioFile_WavBytesToFile(const BYTE *wav, DWORD len, const char *path,
                              int fmt, int channels)
{
    WAVEFORMATEX wf;
    const BYTE  *data;
    DWORD        dataLen;
    BYTE        *expanded = NULL;
    const BYTE  *pcm;
    DWORD        pcmLen;
    WORD         outCh;
    BOOL         ok;
    char        *tmp = NULL;
    const char  *wavTarget;

    if (!WavParseMem(wav, len, &wf, &data, &dataLen)) return FALSE;
    if (channels != 2) channels = 1;

    pcm = data; pcmLen = dataLen; outCh = wf.nChannels;

    /* Up-mix mono 16-bit to stereo by duplicating each sample. */
    if (channels == 2 && wf.nChannels == 1 && wf.wBitsPerSample == 16) {
        DWORD samples = dataLen / 2, i;
        expanded = (BYTE *)Mem_Alloc((SIZE_T)dataLen * 2);
        if (!expanded) return FALSE;
        for (i = 0; i < samples; i++) {
            expanded[i * 4 + 0] = data[i * 2 + 0];
            expanded[i * 4 + 1] = data[i * 2 + 1];
            expanded[i * 4 + 2] = data[i * 2 + 0];
            expanded[i * 4 + 3] = data[i * 2 + 1];
        }
        pcm = expanded; pcmLen = dataLen * 2; outCh = 2;
    }

    /* For WAV we write straight to the destination; for MP3 we need a temp
     * WAV first, then transcode. */
    if (fmt == FMT_MP3) {
        tmp = AudioFile_TempWav();
        if (!tmp) { Mem_Free(expanded); return FALSE; }
        wavTarget = tmp;
    } else {
        wavTarget = path;
    }

    ok = WriteWavFile(wavTarget, outCh, wf.nSamplesPerSec, wf.wBitsPerSample,
                      pcm, pcmLen);
    Mem_Free(expanded);

    if (ok && fmt == FMT_MP3) ok = AudioFile_WavToMp3(tmp, path);
    if (tmp) { DeleteFileA(tmp); Mem_Free(tmp); }
    return ok;
}
