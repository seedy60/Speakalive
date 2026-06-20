/* engine.c - engine manager: detect which speech engines are present on the
 * running system and expose them as an ordered list for the UI's tabs. */
#include "engine.h"

/* Raised by the UI (the Cancel button) to abort an in-progress SaveToFile
 * render; each engine polls it inside its render loop. */
volatile long g_saveCancel = 0;

static SpeechEngine *g_list[4];
static int           g_count;

int Engines_Init(void)
{
    SpeechEngine *cands[3];
    int n = 0, i;

    g_count = 0;

    /* Probe order matches the tab order shown to the user. */
    cands[n++] = Sapi4_Get();
    cands[n++] = Sapi5_Get();
    cands[n++] = OneCore_Get();

    for (i = 0; i < n; i++) {
        if (g_count >= (int)(sizeof(g_list) / sizeof(g_list[0]))) break;
        if (cands[i] && cands[i]->Detect && cands[i]->Detect())
            g_list[g_count++] = cands[i];
    }
    return g_count;
}

SpeechEngine *Engines_Get(int index)
{
    if (index < 0 || index >= g_count) return NULL;
    return g_list[index];
}

int Engines_Count(void)
{
    return g_count;
}

void Engines_ShutdownAll(void)
{
    int i;
    for (i = 0; i < g_count; i++)
        if (g_list[i] && g_list[i]->Shutdown)
            g_list[i]->Shutdown(g_list[i]);
    g_count = 0;
}
