/* test_save.c - throwaway verifier for the SAPI 5 file-render path.
 * Links against the real sapi5.c / audiofile.c / util.c objects. */
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

void __cdecl WinMainCRTStartup(void)
{
    SpeechEngine *e;
    BOOL ok;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    e = Sapi5_Get();
    if (!e->Detect()) { Out("NO SAPI5 PRESENT\n"); ExitProcess(2); }
    e->Init(e);

    ok = e->SaveToFile(e, "Hello from the Speakalive save test.", FALSE,
                       "C:\\git\\Speakalive\\build\\test_mono.wav", FMT_WAV, 1);
    Out(ok ? "WAV mono   : OK\n" : "WAV mono   : FAIL\n");

    ok = e->SaveToFile(e, "Speakalive stereo render.", FALSE,
                       "C:\\git\\Speakalive\\build\\test_stereo.wav", FMT_WAV, 2);
    Out(ok ? "WAV stereo : OK\n" : "WAV stereo : FAIL\n");

    ok = e->SaveToFile(e, "Speakalive mp3 render.", FALSE,
                       "C:\\git\\Speakalive\\build\\test.mp3", FMT_MP3, 1);
    Out(ok ? "MP3        : OK\n" : "MP3        : FAIL (no MP3 encoder - expected on stock Windows)\n");
    e->Shutdown(e);

    /* ---- OneCore (WinRT) ---- */
    {
        SpeechEngine *oc = OneCore_Get();
        if (!oc->Detect()) {
            Out("OneCore    : not present\n");
        } else {
            Voice *v = NULL; int n;
            char c[16];
            oc->Init(oc);
            n = oc->GetVoices(oc, &v);
            IntToStr(n, c, sizeof(c));
            Out("OneCore voices: "); Out(c); Out("\n");
            ok = oc->SaveToFile(oc, "OneCore save test.", FALSE,
                                "C:\\git\\Speakalive\\build\\test_oc.wav", FMT_WAV, 1);
            Out(ok ? "OneCore WAV: OK\n" : "OneCore WAV: FAIL\n");
            ok = oc->SaveToFile(oc, "OneCore stereo.", FALSE,
                                "C:\\git\\Speakalive\\build\\test_oc_stereo.wav", FMT_WAV, 2);
            Out(ok ? "OneCore WAV stereo: OK\n" : "OneCore WAV stereo: FAIL\n");
            /* The bug: plain text with asXml=TRUE used to fail (bare text is
             * not valid SSML).  Should now succeed (wrapped in <speak>). */
            ok = oc->SaveToFile(oc, "Plain text but XML mode is on.", TRUE,
                                "C:\\git\\Speakalive\\build\\test_oc_xml.wav", FMT_WAV, 1);
            Out(ok ? "OneCore XML+plaintext: OK\n" : "OneCore XML+plaintext: FAIL\n");
            /* A real SSML document should still work as-is. */
            ok = oc->SaveToFile(oc,
                "<speak version='1.0' xmlns='http://www.w3.org/2001/10/synthesis' xml:lang='en-US'>full ssml</speak>",
                TRUE, "C:\\git\\Speakalive\\build\\test_oc_ssml.wav", FMT_WAV, 1);
            Out(ok ? "OneCore full-SSML: OK\n" : "OneCore full-SSML: FAIL\n");
            oc->Shutdown(oc);
        }
    }

    /* ---- SAPI 4 ---- */
    {
        SpeechEngine *s4 = Sapi4_Get();
        if (!s4->Detect()) {
            Out("SAPI4      : not present\n");
        } else {
            Voice *v = NULL; int n; char c[16];
            s4->Init(s4);
            n = s4->GetVoices(s4, &v);
            IntToStr(n, c, sizeof(c));
            Out("SAPI4 voices: "); Out(c); Out("\n");
            if (n > 0) { Out("SAPI4 voice[0]: "); Out(v[0].name); Out("\n"); }
            if (s4->Speak(s4, "Speak alive, testing sappy four.", FALSE, NULL))
                Out("SAPI4 Speak: OK\n");
            else
                Out("SAPI4 Speak: FAIL\n");
            Sleep(2500);            /* let it speak before we tear down */

            ok = s4->SaveToFile(s4, "Sappy four file render test.", FALSE,
                                "C:\\git\\Speakalive\\build\\test_s4.wav", FMT_WAV, 1);
            Out(ok ? "SAPI4 WAV mono  : OK\n" : "SAPI4 WAV mono  : FAIL\n");
            ok = s4->SaveToFile(s4, "Sappy four stereo.", FALSE,
                                "C:\\git\\Speakalive\\build\\test_s4_stereo.wav", FMT_WAV, 2);
            Out(ok ? "SAPI4 WAV stereo: OK\n" : "SAPI4 WAV stereo: FAIL\n");

            s4->Shutdown(s4);
        }
    }

    CoUninitialize();
    ExitProcess(0);
}
