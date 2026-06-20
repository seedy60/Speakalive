/* engine.h - speech engine abstraction layer for Speakalive
 *
 * Every speech back-end (SAPI 4, SAPI 5, OneCore) fills in a SpeechEngine
 * vtable.  The UI talks only to this interface, so adding or removing an
 * engine never touches the UI code.  Engines that are not present on the
 * running system report Detect()==FALSE and are simply not shown as a tab.
 */
#ifndef SPEAKALIVE_ENGINE_H
#define SPEAKALIVE_ENGINE_H

#include <windows.h>

#define MAX_VOICE_NAME 128

/* Output file formats for SaveToFile(). */
#define FMT_WAV 0
#define FMT_MP3 1

/* A single selectable voice. 'data'/'index' are interpreted by the back-end. */
typedef struct {
    char  name[MAX_VOICE_NAME];
    void *data;
    int   index;
} Voice;

typedef struct SpeechEngine SpeechEngine;

struct SpeechEngine {
    const char *id;        /* stable short id, e.g. "sapi5"            */
    const char *display;   /* tab caption, e.g. "SAPI 5"              */

    /* Static capabilities (used to enable/disable UI). */
    BOOL supportsPitch;
    BOOL supportsVolume;

    /* Lifecycle. Detect() must be cheap and side-effect free enough to call
     * during start-up; Init() does the heavy COM/WinRT work. */
    BOOL (*Detect)(void);
    BOOL (*Init)(SpeechEngine *e);
    void (*Shutdown)(SpeechEngine *e);

    /* Voice list is owned by the engine; the caller must not free it. */
    int  (*GetVoices)(SpeechEngine *e, Voice **outVoices);
    BOOL (*SetVoice)(SpeechEngine *e, int index);

    /* Normalised parameters.  rate/pitch: -10..10 (0 = default).
     * volume: 0..100. */
    void (*SetRate)(SpeechEngine *e, int rate);
    void (*SetPitch)(SpeechEngine *e, int pitch);
    void (*SetVolume)(SpeechEngine *e, int volume);

    /* Playback.  'text' is ANSI in the system code page.  'asXml' means the
     * user typed engine markup (SAPI 5 XML / SSML) that should be honoured.
     * 'notify' receives WM_APP based progress messages (may be NULL). */
    BOOL (*Speak)(SpeechEngine *e, const char *text, BOOL asXml, HWND notify);
    BOOL (*Pause)(SpeechEngine *e);
    BOOL (*Resume)(SpeechEngine *e);
    BOOL (*Stop)(SpeechEngine *e);
    BOOL (*IsSpeaking)(SpeechEngine *e);

    /* Render to an audio file instead of the speakers. */
    BOOL (*SaveToFile)(SpeechEngine *e, const char *text, BOOL asXml,
                       const char *path, int fmt, int channels);

    void *priv;            /* back-end private state */
};

/* Back-end factories - each returns a pointer to a static SpeechEngine. */
SpeechEngine *Sapi5_Get(void);
SpeechEngine *Sapi4_Get(void);
SpeechEngine *OneCore_Get(void);

/* Engine manager (engine.c). */
int           Engines_Init(void);                 /* returns count detected   */
SpeechEngine *Engines_Get(int index);             /* 0..count-1               */
int           Engines_Count(void);
void          Engines_ShutdownAll(void);

/* Set non-zero to ask an in-progress SaveToFile() to stop early.  Engines poll
 * this in their render loops and return FALSE when it is raised. */
extern volatile long g_saveCancel;

/* Notifications sent to the UI's notify window. */
#define WM_SA_BASE        (WM_APP + 100)
#define WM_SA_WORD        (WM_SA_BASE + 0)  /* wParam=start, lParam=length     */
#define WM_SA_DONE        (WM_SA_BASE + 1)  /* speaking finished               */
#define WM_SA_STARTED     (WM_SA_BASE + 2)  /* speaking actually began         */

/* Private message SAPI 5 posts to the UI when speech events are queued.
 * The UI forwards it straight back to the engine via Sapi5_PumpEvents(). */
#define WM_SA_SAPI5EVENT  (WM_APP + 200)
void Sapi5_PumpEvents(void);

/* Ask the SAPI 5 voice for word-boundary events only when needed (the highlight
 * feature).  Some voices crash generating them, so they are off by default. */
void Sapi5_SetWordHighlight(BOOL on);

/* OneCore's worker thread posts this to the UI when a rendered WAV is ready to
 * play; wParam is a heap path (UI takes ownership), lParam is the generation. */
#define WM_SA_OCPLAY      (WM_APP + 201)
void OneCore_DoPlay(char *path, long gen);

/* SAPI 4's chunk-completion sink posts this to the UI so the next chunk of a
 * long utterance is fed on the UI thread; wParam is the speak generation. */
#define WM_SA_S4NEXT      (WM_APP + 202)
void Sapi4_SpeakNextChunk(long gen);

#endif /* SPEAKALIVE_ENGINE_H */
