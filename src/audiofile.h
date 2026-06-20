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

/* Read a WAV file and return a heap copy of its PCM samples plus the format.
 * Used to stitch several rendered chunks together.  Free *pcm with Mem_Free. */
BOOL  AudioFile_ReadWavPcm(const char *path, BYTE **pcm, DWORD *pcmLen,
                           WAVEFORMATEX *fmt);

/* Write raw PCM (with the given format) to 'path' as FMT_WAV/FMT_MP3 and the
 * requested channel count, reusing the normal mono->stereo / MP3 path. */
BOOL  AudioFile_PcmToFile(const BYTE *pcm, DWORD pcmLen, const WAVEFORMATEX *fmt,
                          const char *path, int outFmt, int channels);

#endif /* SPEAKALIVE_AUDIOFILE_H */
