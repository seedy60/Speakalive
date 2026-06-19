/* diag_sapi4.c - standalone SAPI 4 diagnostic (normal CRT, console). */
#define INITGUID
#include <windows.h>
#include <stdio.h>
#include "speech.h"

int main(void)
{
    HRESULT hr;
    ITTSEnumW *pEnum = NULL;
    ITTSCentralW *pCentral = NULL;
    ITTSAttributesW *pAttr = NULL;
    IAudioMultiMediaDevice *pAudio = NULL;
    TTSMODEINFOW mi;
    ULONG f = 0;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    hr = CoCreateInstance(&CLSID_TTSEnumerator, NULL, CLSCTX_ALL,
                          &IID_ITTSEnumW, (void **)&pEnum);
    printf("CoCreate TTSEnumerator: 0x%08lX  pEnum=%p\n", hr, (void *)pEnum);
    if (!pEnum) return 1;

    pEnum->lpVtbl->Reset(pEnum);
    hr = pEnum->lpVtbl->Next(pEnum, 1, &mi, &f);
    printf("Next: 0x%08lX fetched=%lu name=%ls\n", hr, f, mi.szModeName);

    hr = CoCreateInstance(&CLSID_MMAudioDest, NULL, CLSCTX_ALL,
                          &IID_IAudioMultiMediaDevice, (void **)&pAudio);
    printf("CoCreate MMAudioDest: 0x%08lX pAudio=%p\n", hr, (void *)pAudio);

    hr = pEnum->lpVtbl->Select(pEnum, mi.gModeID, &pCentral, (LPUNKNOWN)pAudio);
    printf("Select: 0x%08lX pCentral=%p\n", hr, (void *)pCentral);

    if (pCentral) {
        WCHAR *t = L"Hello from sappy four.";
        SDATA d;
        GUID none = {0,0,0,{0,0,0,0,0,0,0,0}};
        hr = pCentral->lpVtbl->QueryInterface(pCentral, &IID_ITTSAttributesW, (void **)&pAttr);
        printf("QI ITTSAttributesW: 0x%08lX pAttr=%p\n", hr, (void *)pAttr);

        d.pData = t;
        d.dwSize = (DWORD)((wcslen(t) + 1) * sizeof(WCHAR));

        hr = pCentral->lpVtbl->TextData(pCentral, CHARSET_TEXT, 0, d, NULL,
                                        IID_ITTSBufNotifySink);
        printf("TextData[flags=0, IID=BufNotifySink, NULL sink]: 0x%08lX\n", hr);

        hr = pCentral->lpVtbl->TextData(pCentral, CHARSET_TEXT, TTSDATAFLAG_TAGGED, d,
                                        NULL, IID_ITTSBufNotifySink);
        printf("TextData[flags=TAGGED, IID=BufNotifySink, NULL sink]: 0x%08lX\n", hr);

        d.dwSize = (DWORD)(wcslen(t) * sizeof(WCHAR)); /* no null */
        hr = pCentral->lpVtbl->TextData(pCentral, CHARSET_TEXT, 0, d, NULL,
                                        IID_ITTSBufNotifySink);
        printf("TextData[no-null size]: 0x%08lX\n", hr);

        printf("speaking 4s...\n");
        Sleep(4000);
    }
    return 0;
}
