/* test_vbackup.c - verify the voice-name backup/restore file format round-trips
 * through the registry.  The backup-write and restore-parse cores are copied
 * verbatim from DoBackupVoiceNames/DoRestoreVoiceNames in main.c so this checks
 * the real algorithm.  Uses throwaway registry keys (never the real
 * Software\Speakalive\VoiceNames) and a temp file, and cleans both up. */
#include <windows.h>

static void Out(const char *s){ DWORD w; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, lstrlenA(s), &w, NULL); }
static int g_fail = 0;
static void Check(const char *what, int ok){ char b[256]; wsprintfA(b,"  [%s] %s\n", ok?"PASS":"FAIL", what); Out(b); if(!ok) g_fail=1; }
static int MemEq(const char *a, const char *b, DWORD n){ DWORD i; for(i=0;i<n;i++) if(a[i]!=b[i]) return 0; return 1; }

#define SA_VNBACKUP_TAG "Speakalive Voice Names"
#define SA_VNBACKUP_HDR SA_VNBACKUP_TAG " 1"
#define SRCKEY "Software\\Speakalive\\__VBTEST_SRC"
#define DSTKEY "Software\\Speakalive\\__VBTEST_DST"

/* --- minimal Set/Get mirroring VoiceFriendlySet/Get, but on an arbitrary key - */
static void Set(const char *key, const char *orig, const char *friendly){
    HKEY k;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, key, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL)==ERROR_SUCCESS){
        if (friendly && friendly[0]) RegSetValueExA(k, orig, 0, REG_SZ, (const BYTE*)friendly, (DWORD)lstrlenA(friendly)+1);
        else RegDeleteValueA(k, orig);
        RegCloseKey(k);
    }
}
static void Get(const char *key, const char *orig, char *out, DWORD len){
    HKEY k; DWORD sz=len, type=0; out[0]=0;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, key, 0, KEY_QUERY_VALUE, &k)==ERROR_SUCCESS){
        if (RegQueryValueExA(k, orig, NULL, &type, (BYTE*)out, &sz)!=ERROR_SUCCESS || type!=REG_SZ) out[0]=0;
        RegCloseKey(k);
    }
    out[len-1]=0;
}

/* --- backup core (verbatim shape from DoBackupVoiceNames): enumerate 'key' to a
 * TAB-separated file.  Returns number of entries written. --- */
static int BackupToFile(const char *key, const char *file){
    HKEY k; HANDLE h; DWORD i, written, count=0; char line[1024];
    if (RegOpenKeyExA(HKEY_CURRENT_USER, key, 0, KEY_QUERY_VALUE, &k)!=ERROR_SUCCESS) return -1;
    h = CreateFileA(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h==INVALID_HANDLE_VALUE){ RegCloseKey(k); return -1; }
    WriteFile(h, SA_VNBACKUP_HDR "\r\n", (DWORD)lstrlenA(SA_VNBACKUP_HDR "\r\n"), &written, NULL);
    for (i=0; ; i++){
        char name[512], data[256]; DWORD nameLen=sizeof(name), dataLen=sizeof(data), type=0; LONG r;
        r = RegEnumValueA(k, i, name, &nameLen, NULL, &type, (BYTE*)data, &dataLen);
        if (r==ERROR_NO_MORE_ITEMS) break;
        if (r!=ERROR_SUCCESS) continue;
        if (type!=REG_SZ || name[0]==0) continue;
        data[sizeof(data)-1]=0; if (data[0]==0) continue;
        wsprintfA(line, "%s\t%s\r\n", name, data);
        WriteFile(h, line, (DWORD)lstrlenA(line), &written, NULL);
        count++;
    }
    CloseHandle(h); RegCloseKey(k);
    return (int)count;
}

/* --- restore core (verbatim parser from DoRestoreVoiceNames): parse 'file' into
 * registry key 'key'.  Returns entries applied, or -1 if the header is wrong. -- */
static int RestoreFromFile(const char *key, const char *file){
    HANDLE h; DWORD size, read=0, taglen; char *buf, *p, *limit; int count=0;
    h = CreateFileA(file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h==INVALID_HANDLE_VALUE) return -2;
    size = GetFileSize(h, NULL);
    if (size==INVALID_FILE_SIZE || size==0 || size>0x100000){ CloseHandle(h); return -2; }
    buf = (char*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)size+1);
    if (!buf){ CloseHandle(h); return -2; }
    if (!ReadFile(h, buf, size, &read, NULL)) read=0;
    CloseHandle(h); buf[read]=0;

    taglen = (DWORD)lstrlenA(SA_VNBACKUP_TAG);
    if (read<taglen || !MemEq(buf, SA_VNBACKUP_TAG, taglen)){ HeapFree(GetProcessHeap(),0,buf); return -1; }

    p = buf; limit = buf + read;
    while (p<limit && *p!='\n') p++;
    if (p<limit) p++;
    while (p<limit){
        char *ls=p, *le, *tab;
        while (p<limit && *p!='\n') p++;
        le = p; if (p<limit) p++;
        if (le>ls && le[-1]=='\r') le--;
        *le = 0;
        for (tab=ls; tab<le && *tab!='\t'; tab++) ;
        if (tab<le){ *tab=0; if (ls[0] && tab[1]){ Set(key, ls, tab+1); count++; } }
    }
    HeapFree(GetProcessHeap(),0,buf);
    return count;
}

static void WriteRawBytes(const char *file, const char *bytes, DWORD n){
    HANDLE h = CreateFileA(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD w; if (h!=INVALID_HANDLE_VALUE){ WriteFile(h, bytes, n, &w, NULL); CloseHandle(h); }
}
static void NukeKey(const char *key){ RegDeleteKeyA(HKEY_CURRENT_USER, key); }

void __cdecl WinMainCRTStartup(void){
    char tmp[MAX_PATH], file[MAX_PATH], got[256]; DWORD n;
    int c; HKEY tmpk;

    /* clean slate */
    NukeKey(SRCKEY); NukeKey(DSTKEY);
    n = GetTempPathA(MAX_PATH, tmp); if(!n) lstrcpynA(tmp,".\\",MAX_PATH);
    wsprintfA(file, "%ssa_vbackup_test.savn", tmp);

    Out("== Test A: full round-trip (4 names, special chars) ==\n");
    Set(SRCKEY, "Adult Female #1, American English (TruVoice)", "Bridget");
    Set(SRCKEY, "Microsoft Sam", "Sammy the Snake");
    Set(SRCKEY, "LH Michael", "Mike");
    Set(SRCKEY, "Ludoviko 8000 L", "Telephone Ludo");

    c = BackupToFile(SRCKEY, file);
    Check("backup wrote 4 entries", c==4);
    c = RestoreFromFile(DSTKEY, file);
    Check("restore applied 4 entries", c==4);

    Get(DSTKEY, "Adult Female #1, American English (TruVoice)", got, sizeof(got));
    Check("name with #,() -> Bridget", lstrcmpA(got,"Bridget")==0);
    Get(DSTKEY, "Microsoft Sam", got, sizeof(got));
    Check("friendly with spaces survives", lstrcmpA(got,"Sammy the Snake")==0);
    Get(DSTKEY, "LH Michael", got, sizeof(got));
    Check("LH Michael -> Mike", lstrcmpA(got,"Mike")==0);
    Get(DSTKEY, "Ludoviko 8000 L", got, sizeof(got));
    Check("digits/space name -> Telephone Ludo", lstrcmpA(got,"Telephone Ludo")==0);

    Out("== Test B: parser robustness (LF-only, no trailing newline, junk lines) ==\n");
    NukeKey(DSTKEY);
    {
        /* header, then: valid LF line; a line with no tab (skip); a blank line;
         * a final line with NO trailing newline. */
        static const char raw[] =
            SA_VNBACKUP_TAG " 1\n"
            "Alpha\tFirst\n"
            "no-tab-here\n"
            "\n"
            "Omega\tLast";
        WriteRawBytes(file, raw, (DWORD)(sizeof(raw)-1));
        c = RestoreFromFile(DSTKEY, file);
        Check("applied exactly 2 (junk/blank skipped)", c==2);
        Get(DSTKEY, "Alpha", got, sizeof(got));
        Check("LF-terminated line parsed", lstrcmpA(got,"First")==0);
        Get(DSTKEY, "Omega", got, sizeof(got));
        Check("last line w/o trailing newline parsed", lstrcmpA(got,"Last")==0);
        Get(DSTKEY, "no-tab-here", got, sizeof(got));
        Check("tab-less line ignored", got[0]==0);
    }

    Out("== Test C: bad header rejected ==\n");
    NukeKey(DSTKEY);
    {
        static const char raw[] = "Not A Speakalive File\r\nAlpha\tFirst\r\n";
        WriteRawBytes(file, raw, (DWORD)(sizeof(raw)-1));
        c = RestoreFromFile(DSTKEY, file);
        Check("restore returns -1 (rejected)", c==-1);
        Get(DSTKEY, "Alpha", got, sizeof(got));
        Check("nothing applied from bad file", got[0]==0);
    }

    Out("== Test D: empty registry backs up 0 ==\n");
    NukeKey(SRCKEY);
    if (RegCreateKeyExA(HKEY_CURRENT_USER, SRCKEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &tmpk, NULL)==ERROR_SUCCESS)
        RegCloseKey(tmpk);   /* ensure key exists with no values */
    c = BackupToFile(SRCKEY, file);
    Check("empty key backs up 0 entries", c==0);

    /* cleanup */
    NukeKey(SRCKEY); NukeKey(DSTKEY); DeleteFileA(file);

    Out(g_fail ? "\nRESULT: FAIL\n" : "\nRESULT: ALL PASS\n");
    ExitProcess((UINT)g_fail);
}
