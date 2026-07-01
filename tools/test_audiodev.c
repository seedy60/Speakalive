/* test_audiodev.c - verify the audio-output-device enumeration and the
 * name->index resolution mirrored verbatim from main.c (AudioDeviceResolve).
 * Confirms: a blank name resolves to the system default; each real device
 * name resolves back to its own index; an unknown name falls back to default. */
#include <windows.h>
#include <mmsystem.h>

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }
static int g_fail = 0;
#define AUDIO_DEV_DEFAULT ((UINT)-1)

/* --- verbatim from main.c's AudioDeviceResolve (returns the index instead of
 *     publishing it) --- */
static UINT Resolve(const char *name)
{
    UINT n, i;
    if (name[0] == 0) return AUDIO_DEV_DEFAULT;
    n = waveOutGetNumDevs();
    for (i = 0; i < n; i++) {
        WAVEOUTCAPSA caps;
        if (waveOutGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR &&
            lstrcmpiA(name, caps.szPname) == 0)
            return i;
    }
    return AUDIO_DEV_DEFAULT;
}

static void Check(const char *what, int ok){ char b[160]; wsprintfA(b,"  [%s] %s\n", ok?"PASS":"FAIL", what); Out(b); if(!ok) g_fail=1; }

void __cdecl WinMainCRTStartup(void){
    UINT n, i;
    char b[160];

    n = waveOutGetNumDevs();
    wsprintfA(b, "waveOut devices present: %u\n", n); Out(b);
    for (i = 0; i < n; i++) {
        WAVEOUTCAPSA caps;
        if (waveOutGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            wsprintfA(b, "  [%u] %s\n", i, caps.szPname); Out(b);
        }
    }

    Out("== resolution ==\n");
    Check("blank name -> default", Resolve("") == AUDIO_DEV_DEFAULT);
    Check("unknown name -> default", Resolve("No Such Device 9000") == AUDIO_DEV_DEFAULT);

    /* Every present device must resolve back to its own index. */
    for (i = 0; i < n; i++) {
        WAVEOUTCAPSA caps;
        if (waveOutGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            UINT r = Resolve(caps.szPname);
            wsprintfA(b, "device %u \"%s\" resolves to %u", i, caps.szPname, r);
            Check(b, r == i);
        }
    }
    if (n == 0) Out("  (no devices to round-trip; enumeration path still exercised)\n");

    Out(g_fail ? "\nRESULT: FAIL\n" : "\nRESULT: ALL PASS\n");
    ExitProcess((UINT)g_fail);
}
