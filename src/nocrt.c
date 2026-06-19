/* nocrt.c - freestanding runtime support for Speakalive.
 *
 * Speakalive is linked with /NODEFAULTLIB so the executable does not depend on
 * the Visual C++ runtime (msvcrt or ucrtbase) which does not exist on Windows
 * 2000.  This file supplies the handful of things the compiler and linker
 * still expect: the process entry point, the compiler-helper memory routines,
 * and the floating-point sentinel.
 *
 * IMPORTANT: compile this file with optimisation disabled (/Od).  With
 * optimisation on, MSVC recognises the byte-copy/fill loops below as the
 * memcpy/memset idioms and rewrites them as calls to memcpy/memset - i.e. into
 * infinite recursion.  /Od keeps them as plain loops.
 */
#include <windows.h>

/* Real program entry, implemented in main.c. */
int SpeakaliveMain(void);

/* The PE entry point. /ENTRY:WinMainCRTStartup selects this symbol.
 * __cdecl so the decorated name is _WinMainCRTStartup, which the linker's
 * /ENTRY lookup resolves cleanly on x86. */
void __cdecl WinMainCRTStartup(void)
{
    int code = SpeakaliveMain();
    ExitProcess((UINT)code);
}

/* The linker emits a reference to _fltused for any module that touches
 * floating point.  Define it so /NODEFAULTLIB links cleanly. */
int _fltused = 0x9875;

#pragma function(memset, memcpy, memcmp)

int __cdecl memcmp(const void *a, const void *b, size_t count)
{
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;
    while (count--) {
        if (*p != *q) return (int)*p - (int)*q;
        p++; q++;
    }
    return 0;
}


void *__cdecl memset(void *dst, int val, size_t count)
{
    unsigned char *p = (unsigned char *)dst;
    unsigned char  v = (unsigned char)val;
    while (count--) *p++ = v;
    return dst;
}

void *__cdecl memcpy(void *dst, const void *src, size_t count)
{
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (count--) *d++ = *s++;
    return dst;
}

void *__cdecl memmove(void *dst, const void *src, size_t count)
{
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || count == 0) return dst;
    if (d < s) {
        while (count--) *d++ = *s++;
    } else {
        d += count;
        s += count;
        while (count--) *--d = *--s;
    }
    return dst;
}
