/* util.h - small helpers used throughout Speakalive (no CRT dependency) */
#ifndef SPEAKALIVE_UTIL_H
#define SPEAKALIVE_UTIL_H

#include <windows.h>

/* Heap helpers (process heap, zero-initialised). */
void  *Mem_Alloc(SIZE_T n);
void  *Mem_ReAlloc(void *p, SIZE_T n);
void   Mem_Free(void *p);

/* Code-page conversion.  Returned buffers are heap allocated; free with
 * Mem_Free.  Return NULL on failure. */
WCHAR *AnsiToWide(const char *s);
char  *WideToAnsi(const WCHAR *w);

/* Length-preserving string duplicate (ANSI). */
char  *StrDupA(const char *s);

/* XML-escape an ANSI string (for embedding user text inside markup).
 * Heap allocated, free with Mem_Free. */
char  *XmlEscapeA(const char *s);

/* Simple integer-to-decimal string ("%d" equivalent without the CRT). */
void   IntToStr(int v, char *buf, int bufLen);

#endif /* SPEAKALIVE_UTIL_H */
