/* test_s5out.c - what sample rate does SAPI 5's default audio output use for
 * live speech?  We never call SetOutput, so live speech uses the default audio
 * output object's own format.  Query it directly. */
#include <windows.h>
#include <mmreg.h>
#define COBJMACROS
#include <sapi.h>

int _fltused = 0x9875;
#pragma function(memset, memcpy)
void *__cdecl memset(void *d,int v,size_t n){ unsigned char*p=(unsigned char*)d; while(n--)*p++=(unsigned char)v; return d; }
void *__cdecl memcpy(void *d,const void *s,size_t n){ unsigned char*a=(unsigned char*)d; const unsigned char*b=(const unsigned char*)s; while(n--)*a++=*b++; return d; }

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }
static void OutN(const char *l, long v){ char b[96]; wsprintfA(b,"%s%ld\n",l,v); Out(b); }

void __cdecl WinMainCRTStartup(void)
{
    ISpStreamFormat *fmt = NULL;
    GUID g; WAVEFORMATEX *pw = NULL;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    /* The default multimedia audio output - what ISpVoice uses when no output
     * is set (i.e. live F5 speech). */
    if (FAILED(CoCreateInstance(&CLSID_SpMMAudioOut, NULL, CLSCTX_ALL,
                                &IID_ISpStreamFormat, (void**)&fmt)) || !fmt) {
        Out("could not create SpMMAudioOut / query ISpStreamFormat\n");
        ExitProcess(1);
    }
    if (SUCCEEDED(ISpStreamFormat_GetFormat(fmt, &g, &pw)) && pw) {
        OutN("default audio output sample rate = ", (long)pw->nSamplesPerSec);
        OutN("                       channels  = ", (long)pw->nChannels);
        OutN("                       bits       = ", (long)pw->wBitsPerSample);
        CoTaskMemFree(pw);
    } else {
        Out("GetFormat failed\n");
    }
    ISpStreamFormat_Release(fmt);
    CoUninitialize();
    ExitProcess(0);
}
