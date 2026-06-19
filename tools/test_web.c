/* test_web.c - isolate Web_FetchText from the GUI. */
#include <windows.h>
#include "../src/webread.h"
#include "../src/util.h"

int _fltused = 0x9875;
#pragma function(memset, memcpy)
void *__cdecl memset(void *d,int v,size_t n){ unsigned char*p=(unsigned char*)d; while(n--)*p++=(unsigned char)v; return d; }
void *__cdecl memcpy(void *d,const void *s,size_t n){ unsigned char*a=(unsigned char*)d; const unsigned char*b=(const unsigned char*)s; while(n--)*a++=*b++; return d; }
void *__cdecl memmove(void *d,const void *s,size_t n){ return memcpy(d,s,n); }
int   __cdecl memcmp(const void *a,const void *b,size_t n){ const unsigned char*p=(const unsigned char*)a,*q=(const unsigned char*)b; while(n--){ if(*p!=*q) return (int)*p-(int)*q; p++;q++; } return 0; }

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }

void __cdecl WinMainCRTStartup(void)
{
    char *t = Web_FetchText("http://example.com");
    if (t) {
        char n[16];
        IntToStr(lstrlenA(t), n, sizeof(n));
        Out("OK  len="); Out(n); Out("\n----\n"); Out(t); Out("\n");
        Mem_Free(t);
    } else {
        Out("FETCH FAILED (NULL)\n");
    }
    ExitProcess(0);
}
