/* test_s5out48.c - verify the fixed-48 kHz speaker output binds (sound-free):
 * create a voice, make an SpMMAudioOut, force its format to 48 kHz, read it back,
 * and SetOutput the voice to it.  Confirms the mechanism the live-speak fix uses,
 * so a <voice>-switched voice renders to 48 kHz instead of the UI voice's rate. */
#include <windows.h>
#include <mmreg.h>
#define COBJMACROS
#include <sapi.h>

int _fltused = 0x9875;
#pragma function(memset, memcpy)
void *__cdecl memset(void *d,int v,size_t n){ unsigned char*p=(unsigned char*)d; while(n--)*p++=(unsigned char)v; return d; }
void *__cdecl memcpy(void *d,const void *s,size_t n){ unsigned char*a=(unsigned char*)d; const unsigned char*b=(const unsigned char*)s; while(n--)*a++=*b++; return d; }
int __cdecl memcmp(const void *a,const void *b,size_t n){ const unsigned char*x=(const unsigned char*)a,*y=(const unsigned char*)b; while(n--){ if(*x!=*y)return *x-*y; x++; y++; } return 0; }

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }

void __cdecl WinMainCRTStartup(void){
    ISpVoice *v=NULL; ISpMMSysAudio *au=NULL; char b[160]; HRESULT hr;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    hr = CoCreateInstance(&CLSID_SpVoice, NULL, CLSCTX_ALL, &IID_ISpVoice, (void**)&v);
    wsprintfA(b,"create voice hr=0x%08lx\n",(unsigned long)hr); Out(b);

    hr = CoCreateInstance(&CLSID_SpMMAudioOut, NULL, CLSCTX_ALL, &IID_ISpMMSysAudio, (void**)&au);
    wsprintfA(b,"create SpMMAudioOut hr=0x%08lx\n",(unsigned long)hr); Out(b);
    if(au){
        WAVEFORMATEX wfx; GUID gfmt; WAVEFORMATEX *got=NULL;
        ZeroMemory(&wfx,sizeof(wfx)); wfx.wFormatTag=WAVE_FORMAT_PCM; wfx.nChannels=1;
        wfx.nSamplesPerSec=48000; wfx.wBitsPerSample=16; wfx.nBlockAlign=2; wfx.nAvgBytesPerSec=96000;
        hr = ISpMMSysAudio_SetFormat(au, &SPDFID_WaveFormatEx, &wfx);
        wsprintfA(b,"SetFormat(48000) hr=0x%08lx\n",(unsigned long)hr); Out(b);
        if(SUCCEEDED(ISpStreamFormat_GetFormat((ISpStreamFormat*)au,&gfmt,&got)) && got){
            wsprintfA(b,"read-back format: rate=%lu bits=%u ch=%u\n",
                (unsigned long)got->nSamplesPerSec,(unsigned)got->wBitsPerSample,(unsigned)got->nChannels); Out(b);
            CoTaskMemFree(got);
        }
        if(v){ hr=ISpVoice_SetOutput(v,(IUnknown*)au,FALSE); wsprintfA(b,"SetOutput(voice->48k, no-change) hr=0x%08lx\n",(unsigned long)hr); Out(b); }
    }
    Out((au?"RESULT: fixed 48 kHz output set up OK.\n":"RESULT: could not create SpMMAudioOut.\n"));
    if(v){ ISpVoice_SetOutput(v,NULL,TRUE); ISpVoice_Release(v); }
    if(au) ISpMMSysAudio_Release(au);
    CoUninitialize();
    ExitProcess(0);
}
