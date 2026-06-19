/* util.c - helper implementations (see util.h). */
#include "util.h"

static HANDLE g_heap;

static HANDLE Heap(void)
{
    if (!g_heap) g_heap = GetProcessHeap();
    return g_heap;
}

void *Mem_Alloc(SIZE_T n)
{
    if (n == 0) n = 1;
    return HeapAlloc(Heap(), HEAP_ZERO_MEMORY, n);
}

void *Mem_ReAlloc(void *p, SIZE_T n)
{
    if (n == 0) n = 1;
    if (!p) return Mem_Alloc(n);
    return HeapReAlloc(Heap(), HEAP_ZERO_MEMORY, p, n);
}

void Mem_Free(void *p)
{
    if (p) HeapFree(Heap(), 0, p);
}

WCHAR *AnsiToWide(const char *s)
{
    int    n;
    WCHAR *w;
    if (!s) return NULL;
    n = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    w = (WCHAR *)Mem_Alloc((SIZE_T)n * sizeof(WCHAR));
    if (!w) return NULL;
    MultiByteToWideChar(CP_ACP, 0, s, -1, w, n);
    return w;
}

char *WideToAnsi(const WCHAR *w)
{
    int   n;
    char *s;
    if (!w) return NULL;
    n = WideCharToMultiByte(CP_ACP, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    s = (char *)Mem_Alloc((SIZE_T)n);
    if (!s) return NULL;
    WideCharToMultiByte(CP_ACP, 0, w, -1, s, n, NULL, NULL);
    return s;
}

char *StrDupA(const char *s)
{
    int   n;
    char *d;
    if (!s) return NULL;
    n = lstrlenA(s);
    d = (char *)Mem_Alloc((SIZE_T)n + 1);
    if (!d) return NULL;
    if (n) memcpy(d, s, (SIZE_T)n);
    d[n] = 0;
    return d;
}

char *XmlEscapeA(const char *s)
{
    int   i, n, extra = 0;
    char *out, *p;
    if (!s) return NULL;
    n = lstrlenA(s);
    for (i = 0; i < n; i++) {
        switch (s[i]) {
            case '&':  extra += 4; break; /* &amp;  */
            case '<':  extra += 3; break; /* &lt;   */
            case '>':  extra += 3; break; /* &gt;   */
            case '"':  extra += 5; break; /* &quot; */
            case '\'': extra += 5; break; /* &apos; */
            default: break;
        }
    }
    out = (char *)Mem_Alloc((SIZE_T)n + extra + 1);
    if (!out) return NULL;
    p = out;
    for (i = 0; i < n; i++) {
        switch (s[i]) {
            case '&':  lstrcpyA(p, "&amp;");  p += 5; break;
            case '<':  lstrcpyA(p, "&lt;");   p += 4; break;
            case '>':  lstrcpyA(p, "&gt;");   p += 4; break;
            case '"':  lstrcpyA(p, "&quot;"); p += 6; break;
            case '\'': lstrcpyA(p, "&apos;"); p += 6; break;
            default:   *p++ = s[i]; break;
        }
    }
    *p = 0;
    return out;
}

void IntToStr(int v, char *buf, int bufLen)
{
    char  tmp[16];
    int   i = 0, neg = 0, j = 0;
    if (bufLen <= 0) return;
    if (v < 0) { neg = 1; }
    /* Work in unsigned to handle INT_MIN safely. */
    {
        unsigned int u = neg ? (unsigned int)(-(v + 1)) + 1u : (unsigned int)v;
        if (u == 0) tmp[i++] = '0';
        while (u && i < (int)sizeof(tmp)) { tmp[i++] = (char)('0' + (u % 10u)); u /= 10u; }
    }
    if (neg && j < bufLen - 1) buf[j++] = '-';
    while (i > 0 && j < bufLen - 1) buf[j++] = tmp[--i];
    buf[j] = 0;
}
