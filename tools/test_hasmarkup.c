/* test_hasmarkup.c - verify TextHasMarkup (mirrored verbatim from main.c): a
 * failed speak should only blame the markup when the text actually contains a
 * tag.  The bug this guards: plain text spoken by a voice that genuinely fails
 * must NOT be reported as a markup problem when XML/SSML mode is on. */
#include <windows.h>

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }
static int g_fail = 0;

/* --- verbatim from main.c --- */
static BOOL TextHasMarkup(const char *t)
{
    const char *p;
    for (p = t; *p; p++) {
        if (p[0] == '<') {
            char c = p[1];
            if (c == '/' || c == '?' || c == '!' ||
                (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
                return TRUE;
        }
    }
    return FALSE;
}

static void Expect(const char *text, BOOL want){
    BOOL got = TextHasMarkup(text);
    char b[400];
    wsprintfA(b, "  [%s] %-40s -> %s\n", got==want?"PASS":"FAIL",
              text, got?"markup":"plain");
    Out(b);
    if (got != want) g_fail = 1;
}

void __cdecl WinMainCRTStartup(void){
    Out("== plain text (the Mary/Mike case): must be PLAIN ==\n");
    Expect("Hello world", FALSE);
    Expect("This is a test of Microsoft Mary.", FALSE);
    Expect("It's 75 degrees & sunny today", FALSE);
    Expect("", FALSE);

    Out("== arithmetic / stray '<' not a tag: PLAIN ==\n");
    Expect("5 < 6 apples", FALSE);
    Expect("a < b and c", FALSE);
    Expect("end with a bare <", FALSE);

    Out("== real markup: must be MARKUP ==\n");
    Expect("Hello <emph>there</emph>", TRUE);
    Expect("<rate speed=\"-3\"/>slower", TRUE);
    Expect("done</voice>", TRUE);
    Expect("<speak version=\"1.0\">hi</speak>", TRUE);
    Expect("<?xml version=\"1.0\"?>", TRUE);
    Expect("<!-- a comment -->", TRUE);

    Out(g_fail ? "\nRESULT: FAIL\n" : "\nRESULT: ALL PASS\n");
    ExitProcess((UINT)g_fail);
}
