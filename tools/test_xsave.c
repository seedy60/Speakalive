/* test_xsave.c - verify the background-thread audio render.
 *
 * Mirrors the real app: the engine is created on the "UI" thread, but
 * SaveToFile() is invoked from a SEPARATE worker thread (its own STA), exactly
 * like main.c's SaveThreadProc.  Confirms the cross-thread render works and the
 * output WAV is well-formed, using a long piece of text. */
#include <windows.h>
#include "../src/engine.h"
#include "../src/util.h"

int _fltused = 0x9875;
#pragma function(memset, memcpy)
void *__cdecl memset(void *d, int v, size_t n){ unsigned char*p=(unsigned char*)d; while(n--)*p++=(unsigned char)v; return d; }
void *__cdecl memcpy(void *d, const void *s, size_t n){ unsigned char*a=(unsigned char*)d; const unsigned char*b=(const unsigned char*)s; while(n--)*a++=*b++; return d; }
void *__cdecl memmove(void *d, const void *s, size_t n){ return memcpy(d,s,n); }
int   __cdecl memcmp(const void *a, const void *b, size_t n){ const unsigned char*p=(const unsigned char*)a,*q=(const unsigned char*)b; while(n--){ if(*p!=*q) return (int)*p-(int)*q; p++;q++; } return 0; }

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }

/* shared request, just like SaveReq in main.c */
typedef struct { SpeechEngine *e; const char *text; const char *path; BOOL ok; } Req;

static DWORD WINAPI Worker(LPVOID p)
{
    Req *r = (Req *)p;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    r->ok = r->e->SaveToFile(r->e, r->text, FALSE, r->path, FMT_WAV, 1);
    CoUninitialize();
    return 0;
}

/* Render from a worker thread and report whether the WAV looks valid. */
static void RenderXThread(SpeechEngine *e, const char *text, const char *path, const char *label)
{
    Req    r;
    HANDLE th;
    HANDLE f;
    DWORD  size = 0;
    char   hdr[12];
    DWORD  got = 0;
    char   line[128];

    r.e = e; r.text = text; r.path = path; r.ok = FALSE;
    DeleteFileA(path);

    th = CreateThread(NULL, 0, Worker, &r, 0, NULL);
    if (!th) { Out(label); Out(": CreateThread FAILED\n"); return; }
    WaitForSingleObject(th, 60000);
    CloseHandle(th);

    if (!r.ok) { Out(label); Out(": SaveToFile returned FALSE\n"); return; }

    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) { Out(label); Out(": output file missing\n"); return; }
    size = GetFileSize(f, NULL);
    ReadFile(f, hdr, 12, &got, NULL);
    CloseHandle(f);

    wsprintfA(line, "%s: ok, %lu bytes, hdr=%c%c%c%c..%c%c%c%c %s\n",
        label, (unsigned long)size,
        hdr[0],hdr[1],hdr[2],hdr[3], hdr[8],hdr[9],hdr[10],hdr[11],
        (got==12 && hdr[0]=='R'&&hdr[1]=='I'&&hdr[2]=='F'&&hdr[3]=='F'
                 && hdr[8]=='W'&&hdr[9]=='A'&&hdr[10]=='V'&&hdr[11]=='E'
                 && size > 1000) ? "VALID" : "INVALID");
    Out(line);
}

void __cdecl WinMainCRTStartup(void)
{
    /* a long-ish body, repeated, to mimic "very long text" */
    static char big[8000];
    const char *unit = "Speakalive renders a long passage to an audio file on a "
                       "background thread so the window never freezes. ";
    int ulen = lstrlenA(unit), pos = 0;
    while (pos + ulen < (int)sizeof(big) - 1) { lstrcpyA(big + pos, unit); pos += ulen; }
    big[pos] = 0;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);   /* the "UI" apartment */

    /* ---- SAPI 5: engine created here, rendered on a worker thread ---- */
    {
        SpeechEngine *e = Sapi5_Get();
        if (!e->Detect()) Out("SAPI5: not present\n");
        else {
            e->Init(e);
            RenderXThread(e, big, "C:\\git\\Speakalive\\build\\xsave_s5.wav", "SAPI5 xthread");
            e->Shutdown(e);
        }
    }

    /* ---- OneCore ---- */
    {
        SpeechEngine *oc = OneCore_Get();
        if (!oc->Detect()) Out("OneCore: not present\n");
        else {
            Voice *v = NULL;
            oc->Init(oc);
            oc->GetVoices(oc, &v);
            RenderXThread(oc, big, "C:\\git\\Speakalive\\build\\xsave_oc.wav", "OneCore xthread");
            oc->Shutdown(oc);
        }
    }

    /* ---- SAPI 4 ---- */
    {
        SpeechEngine *s4 = Sapi4_Get();
        if (!s4->Detect()) Out("SAPI4: not present\n");
        else {
            Voice *v = NULL;
            s4->Init(s4);
            s4->GetVoices(s4, &v);
            RenderXThread(s4, big, "C:\\git\\Speakalive\\build\\xsave_s4.wav", "SAPI4 xthread");
            s4->Shutdown(s4);
        }
    }

    CoUninitialize();
    ExitProcess(0);
}
