// hostaudio.h: minimal host PCM sink (AudioQueue) used for movie audio. Signed 16-bit interleaved samples. Thread-safe. Implemented in hostaudio.m.
#ifndef HOSTAUDIO_H
#define HOSTAUDIO_H
int ha_open(int rate, int channels); // (re)start the output; 0 on success
void ha_push(const short *s,
             int frames);  // queue frames (oldest samples are dropped if ~2s are already buffered)
void ha_flush(void);       // drop everything queued
void ha_set_gain(float g); // 0..1
void ha_pause(int p);
void ha_close(void);
int ha2_open(int rate, int channels); // second, independent sink: the game's own mixer output
void ha2_push(const short *s, int frames);
#endif
