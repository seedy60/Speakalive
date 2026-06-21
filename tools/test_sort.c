/* test_sort.c - verify CBS_SORT combo + item-data maps display order back to
 * the real engine voice index.  Uses a hidden window (never shown). */
#include <windows.h>

int _fltused = 0x9875;
#pragma function(memset, memcpy)
void *__cdecl memset(void *d,int v,size_t n){ unsigned char*p=(unsigned char*)d; while(n--)*p++=(unsigned char)v; return d; }
void *__cdecl memcpy(void *d,const void *s,size_t n){ unsigned char*a=(unsigned char*)d; const unsigned char*b=(const unsigned char*)s; while(n--)*a++=*b++; return d; }

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }

/* same order the OneCore engine enumerated them earlier */
static const char *names[] = {
    "Microsoft David","Microsoft George","Microsoft Linda","Microsoft Hazel",
    "Microsoft Susan","Microsoft Zira","Microsoft Mark"
};
#define N (int)(sizeof(names)/sizeof(names[0]))

void __cdecl WinMainCRTStartup(void)
{
    HINSTANCE hi = GetModuleHandleA(NULL);
    HWND parent, combo;
    int i;
    char line[128];

    parent = CreateWindowExA(0, "STATIC", "", 0, 0, 0, 10, 10, NULL, NULL, hi, NULL);
    combo  = CreateWindowExA(0, "COMBOBOX", "",
                 WS_CHILD | CBS_DROPDOWNLIST | CBS_SORT, 0, 0, 100, 100,
                 parent, NULL, hi, NULL);

    for (i = 0; i < N; i++) {
        int pos = (int)SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)names[i]);
        SendMessageA(combo, CB_SETITEMDATA, pos, (LPARAM)i);   /* real index */
    }

    Out("display order (text -> stored real index -> original name):\n");
    {
        int n = (int)SendMessageA(combo, CB_GETCOUNT, 0, 0), ok = 1;
        char prev[128]; prev[0] = 0;
        for (i = 0; i < n; i++) {
            char txt[128];
            int real;
            SendMessageA(combo, CB_GETLBTEXT, i, (LPARAM)txt);
            real = (int)SendMessageA(combo, CB_GETITEMDATA, i, 0);
            wsprintfA(line, "  %2d  %-22s  real=%d  -> %s\n", i, txt, real, names[real]);
            Out(line);
            if (lstrcmpiA(txt, names[real]) != 0) ok = 0;        /* mapping wrong */
            if (prev[0] && lstrcmpiA(prev, txt) > 0) ok = 0;     /* not sorted    */
            lstrcpynA(prev, txt, sizeof(prev));
        }
        Out(ok ? "RESULT: sorted and mapping correct\n" : "RESULT: FAIL\n");
    }
    ExitProcess(0);
}
