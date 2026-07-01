/* test_voiceswitch.c - verify HasVoiceSwitch (mirrored verbatim from sapi5.c),
 * which decides whether a SAPI 5 utterance needs the fixed 48 kHz output.  It
 * must be TRUE only when the text actually contains a <voice> switch, so normal
 * speech uses the default (no-resampler) output that pauses/resumes cleanly. */
#include <windows.h>

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }
static int g_fail = 0;

/* --- verbatim from sapi5.c --- */
static BOOL HasVoiceSwitch(const char *t)
{
    const char *p;
    for (p = t; *p; p++) {
        if (p[0] == '<' &&
            (p[1] == 'v' || p[1] == 'V') && (p[2] == 'o' || p[2] == 'O') &&
            (p[3] == 'i' || p[3] == 'I') && (p[4] == 'c' || p[4] == 'C') &&
            (p[5] == 'e' || p[5] == 'E'))
            return TRUE;
    }
    return FALSE;
}

static void Expect(const char *text, BOOL want){
    BOOL got = HasVoiceSwitch(text);
    char b[300];
    wsprintfA(b, "  [%s] %-46s -> %s\n", got==want?"PASS":"FAIL", text, got?"48k":"default");
    Out(b);
    if (got != want) g_fail = 1;
}

void __cdecl WinMainCRTStartup(void){
    Out("== needs 48 kHz: real <voice> switch ==\n");
    Expect("<voice required=\"Name=IVONA 2 Brian\">Hi</voice>", TRUE);
    Expect("before <VOICE required=\"...\">x</VOICE> after", TRUE);
    Expect("<Voice>x</Voice>", TRUE);

    Out("== default output: no <voice> switch ==\n");
    Expect("Hello world", FALSE);
    Expect("<rate speed=\"-3\"/>a little slower", FALSE);
    Expect("<emph>there</emph>", FALSE);
    Expect("send me an invoice please", FALSE);   /* 'invoice' has no '<' */
    Expect("", FALSE);
    Expect("<v", FALSE);                            /* too short, must not overrun */
    Expect("ends with a bare <", FALSE);
    Expect("<voic", FALSE);                         /* not quite <voice */

    Out(g_fail ? "\nRESULT: FAIL\n" : "\nRESULT: ALL PASS\n");
    ExitProcess((UINT)g_fail);
}
