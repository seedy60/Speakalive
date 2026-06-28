/* test_markup.c - exercise CheckMarkup (copied verbatim from main.c) against the
 * specific mistakes a user asked about: missing closing tag, too many/few quote
 * marks, a '<' that should be '>', plus valid markup and mismatches. */
#include <windows.h>

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }

/* ---- verbatim copy of CheckMarkup from main.c ---- */
static char g_mkMsg[256];
static char g_mkStack[32][64];
static const char *CheckMarkup(const char *t)
{
    int depth = 0, i = 0;
    while (t[i]) {
        if (t[i] != '<') { i++; continue; }
        if (t[i + 1] == '?' || t[i + 1] == '!') {
            i += 2;
            while (t[i] && t[i] != '>') i++;
            if (!t[i]) { lstrcpynA(g_mkMsg, "A '<?' or '<!' section was opened but never closed with '>'.", sizeof(g_mkMsg)); return g_mkMsg; }
            i++;
            continue;
        }
        if (t[i + 1] == ' ' || t[i + 1] == '\t' || t[i + 1] == '\r' ||
            t[i + 1] == '\n' || t[i + 1] == '>' || t[i + 1] == 0) {
            lstrcpynA(g_mkMsg, "A '<' is followed by a space or nothing, so it reads as a literal less-than sign. To speak a literal <, write it as &lt;.", sizeof(g_mkMsg));
            return g_mkMsg;
        }
        {
            int  isClose = (t[i + 1] == '/');
            char name[64];
            int  nl = 0, selfClose = 0;
            int  j = i + 1 + (isClose ? 1 : 0);
            char q = 0;
            while (t[j] == ' ' || t[j] == '\t') j++;
            while (t[j] && t[j] != ' ' && t[j] != '\t' && t[j] != '>' && t[j] != '/' &&
                   t[j] != '<' && t[j] != '\r' && t[j] != '\n') { if (nl < 63) name[nl++] = t[j]; j++; }
            name[nl] = 0;
            if (nl == 0 && !isClose) { lstrcpynA(g_mkMsg, "A '<' is not followed by a tag name.", sizeof(g_mkMsg)); return g_mkMsg; }
            while (t[j]) {
                char c = t[j];
                if (q) { if (c == q) q = 0; j++; continue; }
                if (c == '"' || c == '\'') { q = c; j++; continue; }
                if (c == '<') { wsprintfA(g_mkMsg, "Inside the tag <%s ...> there is a '<' before the '>' that should close it. Did you mean '>', or is a '>' missing?", name); return g_mkMsg; }
                if (c == '>') { if (t[j - 1] == '/') selfClose = 1; break; }
                j++;
            }
            if (!t[j]) {
                if (q) wsprintfA(g_mkMsg, "A quote mark is not matched in the tag <%s ...> - check for a missing or extra quote around an attribute value.", name);
                else   wsprintfA(g_mkMsg, "The tag <%s ...> was opened with '<' but never closed with '>'.", name);
                return g_mkMsg;
            }
            i = j + 1;
            if (isClose) {
                if (depth == 0) { wsprintfA(g_mkMsg, "There is a closing tag </%s> with no matching opening tag before it.", name); return g_mkMsg; }
                if (lstrcmpA(g_mkStack[depth - 1], name) != 0) { wsprintfA(g_mkMsg, "The closing tag </%s> does not match the open tag <%s> - tags must be closed in the order they were opened.", name, g_mkStack[depth - 1]); return g_mkMsg; }
                depth--;
            } else if (!selfClose) { if (depth < 32) lstrcpynA(g_mkStack[depth++], name, 64); }
        }
    }
    if (depth > 0) { wsprintfA(g_mkMsg, "The tag <%s> was opened but never closed - add a matching </%s>.", g_mkStack[depth - 1], g_mkStack[depth - 1]); return g_mkMsg; }
    return NULL;
}

static void test(const char *label, const char *t){
    const char *r = CheckMarkup(t);
    char b[400];
    wsprintfA(b, "%-22s | %-30s | %s\n", label, t, r ? r : "(ok - well-formed)");
    Out(b);
}

void __cdecl WinMainCRTStartup(void){
    Out("case                   | input                          | result\n");
    Out("-----------------------+--------------------------------+---------------------------\n");
    test("valid prosody",       "<prosody rate=\"slow\">hi</prosody>");
    test("valid self-close",    "before <break time=\"500ms\"/> after");
    test("plain text",          "just some text, 5 > 3 even");
    test("missing closing tag", "<emphasis>hello there");
    test("too few quotes",      "<prosody rate=\"slow>hi</prosody>");
    test("too many quotes",     "<prosody rate=\"slow\"\">hi</prosody>");
    test("< instead of >",      "<prosody rate=\"slow\"<hi");
    test("mismatched tags",     "<a><b>x</a></b>");
    test("stray close tag",     "hello</prosody>");
    test("no tag name",         "a < b is true");
    ExitProcess(0);
}
