/* audiofile.h - WAV/MP3 helpers shared by the engine back-ends. */
#ifndef SPEAKALIVE_AUDIOFILE_H
#define SPEAKALIVE_AUDIOFILE_H

#include <windows.h>

/* Create a unique temporary .wav path (a real empty file is created).
 * Returns a heap string (free with Mem_Free) or NULL. */
char *AudioFile_TempWav(void);

/* Transcode an existing PCM .wav to .mp3 (ACM encoder, then lame.exe). */
BOOL  AudioFile_WavToMp3(const char *wavPath, const char *mp3Path);

/* Take a complete in-memory RIFF/WAVE image and write it to 'path' as the
 * requested format (FMT_WAV/FMT_MP3) and channel count (1 mono, 2 stereo).
 * Mono sources are up-mixed to stereo by sample duplication when needed. */
BOOL  AudioFile_WavBytesToFile(const BYTE *wav, DWORD len, const char *path,
                               int fmt, int channels);

#endif /* SPEAKALIVE_AUDIOFILE_H */
