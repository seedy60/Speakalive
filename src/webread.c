/* webread.c - download a web page (WinINet) and turn it into readable text.
 *
 * Like Speakonia's "read web page", this is a pragmatic HTML-to-text reducer,
 * not a full parser: it drops <script>/<style>/comments, turns tags into line
 * breaks or spaces, decodes the common entities, and collapses whitespace.
 * WinINet is used because it ships on every Windows from 2000 up, handles
 * HTTPS and redirects, and honours the system proxy. */
#include <windows.h>
#include <wininet.h>

#include "util.h"
#include "webread.h"

#define MAX_FETCH (16u * 1024u * 1024u)   /* refuse pages larger than 16 MB */

/* ---- growable byte buffer -------------------------------------------- */

typedef struct { char *buf; DWORD len, cap; } Buf;

static int BufInit(Buf *b)
{
    b->cap = 8192; b->len = 0;
    b->buf = (char *)Mem_Alloc(b->cap);
    return b->buf != NULL;
}
static void BufCh(Buf *b, char c)
{
    if (b->len + 1 >= b->cap) {
        DWORD nc = b->cap * 2;
        char *nb = (char *)Mem_ReAlloc(b->buf, nc);
        if (!nb) return;
        b->buf = nb; b->cap = nc;
    }
    b->buf[b->len++] = c;
}
static void BufStr(Buf *b, const char *s) { while (*s) BufCh(b, *s++); }

/* ---- small ASCII helpers --------------------------------------------- */

static BOOL StartsWithCI(const char *s, const char *pre)
{
    while (*pre) {
        char a = *s++, p = *pre++;
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (p >= 'A' && p <= 'Z') p = (char)(p + 32);
        if (a != p) return FALSE;
    }
    return TRUE;
}
static const char *FindCI(const char *hay, const char *needle)
{
    for (; *hay; hay++) if (StartsWithCI(hay, needle)) return hay;
    return NULL;
}
static int IsHex(char c)
{ return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static int HexVal(char c)
{ if (c <= '9') return c - '0'; if (c <= 'F') return c - 'A' + 10; return c - 'a' + 10; }

static void Utf8Emit(Buf *b, unsigned int cp)
{
    if (cp == 0) return;
    if (cp < 0x80) BufCh(b, (char)cp);
    else if (cp < 0x800) {
        BufCh(b, (char)(0xC0 | (cp >> 6)));
        BufCh(b, (char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        BufCh(b, (char)(0xE0 | (cp >> 12)));
        BufCh(b, (char)(0x80 | ((cp >> 6) & 0x3F)));
        BufCh(b, (char)(0x80 | (cp & 0x3F)));
    } else {
        BufCh(b, (char)(0xF0 | (cp >> 18)));
        BufCh(b, (char)(0x80 | ((cp >> 12) & 0x3F)));
        BufCh(b, (char)(0x80 | ((cp >> 6) & 0x3F)));
        BufCh(b, (char)(0x80 | (cp & 0x3F)));
    }
}

/* Tags that should produce a line break in the output. */
static BOOL IsBlockTag(const char *p)
{
    static const char *blk[] = {
        "p","br","div","tr","li","ul","ol","h1","h2","h3","h4","h5","h6",
        "table","blockquote","hr","pre","section","article","header","footer",
        "td","th","dd","dt","figure","nav","aside","main", NULL
    };
    int i;
    if (*p == '/') p++;
    for (i = 0; blk[i]; i++) {
        if (StartsWithCI(p, blk[i])) {
            char after = p[lstrlenA(blk[i])];
            if (after == '>' || after == ' ' || after == '/' || after == '\t' ||
                after == '\r' || after == '\n' || after == 0)
                return TRUE;
        }
    }
    return FALSE;
}

/* Decode an entity at 'p' (which points at '&').  Emits the result into 'out'
 * and returns the number of source chars consumed, or 0 if not an entity. */
static int DecodeEntity(const char *p, Buf *out)
{
    const char *q = p + 1;
    if (*q == '#') {
        unsigned int code = 0;
        q++;
        if (*q == 'x' || *q == 'X') { q++; while (IsHex(*q)) { code = code * 16 + (unsigned)HexVal(*q); q++; } }
        else { while (*q >= '0' && *q <= '9') { code = code * 10 + (unsigned)(*q - '0'); q++; } }
        if (*q == ';') q++;
        Utf8Emit(out, code);
        return (int)(q - p);
    }
    if (StartsWithCI(q, "amp;"))   { BufCh(out, '&');  return 5; }
    if (StartsWithCI(q, "lt;"))    { BufCh(out, '<');  return 4; }
    if (StartsWithCI(q, "gt;"))    { BufCh(out, '>');  return 4; }
    if (StartsWithCI(q, "quot;"))  { BufCh(out, '"');  return 6; }
    if (StartsWithCI(q, "apos;"))  { BufCh(out, '\''); return 6; }
    if (StartsWithCI(q, "nbsp;"))  { BufCh(out, ' ');  return 6; }
    if (StartsWithCI(q, "mdash;")) { BufStr(out, "-"); return 7; }
    if (StartsWithCI(q, "ndash;")) { BufStr(out, "-"); return 7; }
    if (StartsWithCI(q, "hellip;")){ BufStr(out, "..."); return 8; }
    return 0;
}

/* ---- HTML -> text ---------------------------------------------------- */

static char *HtmlToText(const char *html)
{
    Buf out;
    const char *p = html;
    int pendingNL = 0, pendingSP = 0, haveContent = 0, haveAnyLine = 0;
    if (!BufInit(&out)) return NULL;

    while (*p) {
        if (*p == '<') {
            const char *t = p + 1;
            if (StartsWithCI(t, "script") || StartsWithCI(t, "style")) {
                const char *close = StartsWithCI(t, "script") ? "</script" : "</style";
                const char *e = FindCI(p + 1, close);
                if (!e) break;
                while (*e && *e != '>') e++;
                p = (*e == '>') ? e + 1 : e;
                continue;
            }
            if (StartsWithCI(t, "!--")) {
                const char *e = p;
                while (*e && !(e[0] == '-' && e[1] == '-' && e[2] == '>')) e++;
                p = *e ? e + 3 : e;
                continue;
            }
            if (IsBlockTag(t)) { pendingNL = 1; pendingSP = 0; }
            while (*p && *p != '>') p++;
            if (*p == '>') p++;
            continue;
        }

        /* About to emit real text - flush any pending break/space first. */
        if (*p == '&' || (unsigned char)*p > ' ') {
            if (pendingNL) {
                if (haveAnyLine) BufStr(&out, "\r\n");
                pendingNL = 0; pendingSP = 0; haveContent = 0;
            } else if (pendingSP) {
                if (haveContent) BufCh(&out, ' ');
                pendingSP = 0;
            }
        }

        if (*p == '&') {
            int c = DecodeEntity(p, &out);
            if (c > 0) { p += c; haveContent = 1; haveAnyLine = 1; continue; }
            BufCh(&out, '&'); p++; haveContent = 1; haveAnyLine = 1;
            continue;
        }
        if ((unsigned char)*p <= ' ') { pendingSP = 1; p++; continue; }

        BufCh(&out, *p++);
        haveContent = 1; haveAnyLine = 1;
    }

    BufCh(&out, 0);   /* NUL terminate */
    return out.buf;
}

/* ---- networking ------------------------------------------------------ */

static BOOL FetchRaw(const char *url, char **outBuf, DWORD *outLen)
{
    HINTERNET hNet, hUrl;
    char *buf;
    DWORD  cap = 65536, len = 0;

    *outBuf = NULL; *outLen = 0;
    hNet = InternetOpenA("Speakalive", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hNet) return FALSE;
    hUrl = InternetOpenUrlA(hNet, url, NULL, 0,
                            INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_UI |
                            INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hUrl) { InternetCloseHandle(hNet); return FALSE; }

    buf = (char *)Mem_Alloc(cap);
    if (!buf) { InternetCloseHandle(hUrl); InternetCloseHandle(hNet); return FALSE; }

    for (;;) {
        DWORD got = 0;
        if (cap - len < 8192) {
            DWORD nc = cap * 2;
            char *nb;
            if (nc > MAX_FETCH) nc = MAX_FETCH;
            if (nc <= cap) break;          /* size cap reached */
            nb = (char *)Mem_ReAlloc(buf, nc);
            if (!nb) break;
            buf = nb; cap = nc;
        }
        if (!InternetReadFile(hUrl, buf + len, cap - len - 1, &got)) break;
        if (got == 0) break;               /* end of stream */
        len += got;
    }
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hNet);

    buf[len] = 0;
    *outBuf = buf; *outLen = len;
    return len > 0;
}

/* Convert downloaded bytes (assumed UTF-8, falling back to the system code
 * page) to an ANSI string for the edit control. */
static char *ToAnsi(const char *s)
{
    int  wn;
    UINT cp = CP_UTF8;
    DWORD fl = MB_ERR_INVALID_CHARS;
    WCHAR *w;
    char  *a;

    wn = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    if (wn <= 0) { cp = CP_ACP; fl = 0; wn = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0); }
    if (wn <= 0) return NULL;
    w = (WCHAR *)Mem_Alloc((SIZE_T)wn * sizeof(WCHAR));
    if (!w) return NULL;
    MultiByteToWideChar(cp, fl, s, -1, w, wn);
    a = WideToAnsi(w);
    Mem_Free(w);
    return a;
}

char *Web_FetchText(const char *url)
{
    char *raw, *text, *ansi;
    DWORD rawLen;
    if (!FetchRaw(url, &raw, &rawLen)) return NULL;
    text = HtmlToText(raw);
    Mem_Free(raw);
    if (!text) return NULL;
    ansi = ToAnsi(text);
    Mem_Free(text);
    return ansi;
}
