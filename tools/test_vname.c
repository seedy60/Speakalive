/* test_vname.c - verify friendly-name registry round-trip with a real, special-
 * character voice name as the value key (set -> get -> clear -> get), and leave
 * the registry clean.  Mirrors VoiceFriendlyGet/Set in main.c. */
#include <windows.h>

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }
#define SA_VOICEKEY "Software\\Speakalive\\VoiceNames"

static void Get(const char *orig, char *out, DWORD len){
    HKEY k; DWORD sz=len, type=0; out[0]=0;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, SA_VOICEKEY, 0, KEY_QUERY_VALUE, &k)==ERROR_SUCCESS){
        if (RegQueryValueExA(k, orig, NULL, &type, (BYTE*)out, &sz)!=ERROR_SUCCESS || type!=REG_SZ) out[0]=0;
        RegCloseKey(k);
    }
    out[len-1]=0;
}
static void Set(const char *orig, const char *friendly){
    HKEY k;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, SA_VOICEKEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL)==ERROR_SUCCESS){
        if (friendly && friendly[0]) RegSetValueExA(k, orig, 0, REG_SZ, (const BYTE*)friendly, (DWORD)lstrlenA(friendly)+1);
        else RegDeleteValueA(k, orig);
        RegCloseKey(k);
    }
}

void __cdecl WinMainCRTStartup(void){
    const char *orig = "Adult Female #1, American English (TruVoice)";
    char got[128]; char b[256];

    Set(orig, "Bridget");
    Get(orig, got, sizeof(got));
    wsprintfA(b,"after set : \"%s\" -> \"%s\"  %s\n", orig, got,
              lstrcmpA(got,"Bridget")==0 ? "OK" : "FAIL"); Out(b);

    Set(orig, "");           /* clear */
    Get(orig, got, sizeof(got));
    wsprintfA(b,"after clear: \"%s\" -> \"%s\"  %s\n", orig, got,
              got[0]==0 ? "OK (reverts to original)" : "FAIL"); Out(b);

    ExitProcess(0);
}
