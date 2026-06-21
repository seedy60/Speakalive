/* test_chunk.c - validate the two engine-agnostic halves of SAPI 4 chunking:
 *   1. the chunk splitter tiles the text exactly (no gaps / overlaps), and
 *   2. AudioFile_ReadWavPcm + AudioFile_PcmToFile stitch several WAVs into one
 *      correct combined WAV (data length == sum of the parts).
 * The per-chunk SAPI 4 render itself can only be checked on SAPI 4 hardware. */
#include <windows.h>
#include "../src/engine.h"
#include "../src/util.h"
#include "../src/audiofile.h"

int _fltused = 0x9875;
#pragma function(memset, memcpy)
void *__cdecl memset(void *d,int v,size_t n){ unsigned char*p=(unsigned char*)d; while(n--)*p++=(unsigned char)v; return d; }
void *__cdecl memcpy(void *d,const void *s,size_t n){ unsigned char*a=(unsigned char*)d; const unsigned char*b=(const unsigned char*)s; while(n--)*a++=*b++; return d; }
void *__cdecl memmove(void *d,const void *s,size_t n){ return memcpy(d,s,n); }
int   __cdecl memcmp(const void*a,const void*b,size_t n){ const unsigned char*p=a,*q=b; while(n--){ if(*p!=*q)return *p-*q; p++;q++; } return 0; }

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }
static void OutN(const char *l, long v){ char b[80]; wsprintfA(b,"%s%ld\n",l,v); Out(b); }

/* ---- copy of the splitter from sapi4.c (kept in sync) ---- */
#define S4_CHUNK_CHARS 4000
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

static void TestSplit(void)
{
    static WCHAR big[200000];
    const WCHAR *unit = L"This is Seediffusion. A sentence with words, and punctuation! Another? Yes. ";
    int ulen = lstrlenW(unit), p = 0, total, pos, nchunks = 0, covered = 0, bad = 0;
    while (p + ulen < (int)(sizeof(big)/sizeof(big[0])) - 1) { lstrcpyW(big + p, unit); p += ulen; }
    big[p] = 0;
    total = lstrlenW(big);

    for (pos = 0; pos < total; ) {
        int clen = S4_ChunkLen(big, pos, total, S4_CHUNK_CHARS);
        if (clen <= 0 || clen > S4_CHUNK_CHARS) bad = 1;
        covered += clen;
        nchunks++;
        pos += clen;
    }
    OutN("split: total chars   = ", total);
    OutN("split: chunk count   = ", nchunks);
    OutN("split: chars covered = ", covered);
    Out((covered == total && !bad) ? "split: RESULT tiles exactly, all <= 4000 (good)\n"
                                    : "split: RESULT FAIL\n");
}

/* ---- stitch test using real SAPI 5 WAVs ---- */
static void TestStitch(void)
{
    SpeechEngine *e = Sapi5_Get();
    const char *p1 = "C:\\git\\Speakalive\\build\\ck1.wav";
    const char *p2 = "C:\\git\\Speakalive\\build\\ck2.wav";
    const char *p3 = "C:\\git\\Speakalive\\build\\ck3.wav";
    const char *pc = "C:\\git\\Speakalive\\build\\ck_all.wav";
    BYTE *a=NULL,*b=NULL,*c=NULL,*all=NULL,*back=NULL; DWORD la=0,lb=0,lc=0,lall=0,lback=0;
    WAVEFORMATEX fa,fb,fc,fback;

    if (!e->Detect()) { Out("stitch: SAPI5 not present, skipped\n"); return; }
    e->Init(e);
    e->SaveToFile(e, "First chunk of speech.",  FALSE, p1, FMT_WAV, 1);
    e->SaveToFile(e, "Second chunk of speech.", FALSE, p2, FMT_WAV, 1);
    e->SaveToFile(e, "Third chunk of speech.",  FALSE, p3, FMT_WAV, 1);

    if (!AudioFile_ReadWavPcm(p1,&a,&la,&fa) ||
        !AudioFile_ReadWavPcm(p2,&b,&lb,&fb) ||
        !AudioFile_ReadWavPcm(p3,&c,&lc,&fc)) { Out("stitch: ReadWavPcm FAILED\n"); goto done; }

    lall = la + lb + lc;
    all  = (BYTE *)Mem_Alloc(lall);
    memcpy(all, a, la); memcpy(all+la, b, lb); memcpy(all+la+lb, c, lc);

    if (!AudioFile_PcmToFile(all, lall, &fa, pc, FMT_WAV, 1)) { Out("stitch: PcmToFile FAILED\n"); goto done; }
    if (!AudioFile_ReadWavPcm(pc,&back,&lback,&fback)) { Out("stitch: re-read FAILED\n"); goto done; }

    OutN("stitch: part PCM bytes   = ", (long)lall);
    OutN("stitch: combined PCM bytes= ", (long)lback);
    Out((lback == lall && fback.nChannels == fa.nChannels &&
         fback.nSamplesPerSec == fa.nSamplesPerSec) ?
        "stitch: RESULT combined = sum of parts, format preserved (good)\n" :
        "stitch: RESULT FAIL\n");

done:
    Mem_Free(a); Mem_Free(b); Mem_Free(c); Mem_Free(all); Mem_Free(back);
    DeleteFileA(p1); DeleteFileA(p2); DeleteFileA(p3); DeleteFileA(pc);
    e->Shutdown(e);
}

void __cdecl WinMainCRTStartup(void)
{
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    TestSplit();
    TestStitch();
    CoUninitialize();
    ExitProcess(0);
}
