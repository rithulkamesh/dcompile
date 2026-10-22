// hostaudio.m: AudioQueue PCM sinks. Sink 0 = movie audio (ha_*), sink 1 = the game's own mixer output (ha2_*). Compile with -fno-objc-arc; needs -framework AudioToolbox.
#include <AudioToolbox/AudioToolbox.h>
#include <pthread.h>
#include <dispatch/dispatch.h>
#include <string.h>
#include "hostaudio.h"

#define NBUF 3
#define BUF_FRAMES 2048
typedef struct {
    AudioQueueRef q;
    AudioQueueBufferRef bufs[NBUF];
    int rate, chans, paused;
    float gain;
    short *ring;
    int ring_cap, ring_r, ring_n;
    pthread_mutex_t mu;
} Sink;
static Sink sinks[2] = {{.gain = 1, .mu = PTHREAD_MUTEX_INITIALIZER},
                        {.gain = 1, .mu = PTHREAD_MUTEX_INITIALIZER}};

static void fill(void *u, AudioQueueRef aq, AudioQueueBufferRef b) {
    Sink *s = u;
    short *out = b->mAudioData;
    int want = BUF_FRAMES, got = 0, chans = s->chans;
    pthread_mutex_lock(&s->mu);
    while (s->ring && !s->paused && got < want && s->ring_n) {
        int n = s->ring_cap - s->ring_r;
        if (n > s->ring_n)
            n = s->ring_n;
        if (n > want - got)
            n = want - got;
        memcpy(out + got * chans, s->ring + s->ring_r * chans, n * chans * sizeof(short));
        s->ring_r = (s->ring_r + n) % s->ring_cap;
        s->ring_n -= n;
        got += n;
    }
    float g = s->gain;
    pthread_mutex_unlock(&s->mu);
    memset(out + got * chans, 0, (want - got) * chans * sizeof(short));
    if (g < 0.999f)
        for (int i = 0; i < got * chans; i++)
            out[i] = (short)(out[i] * g);
    b->mAudioDataByteSize = want * chans * sizeof(short);
    AudioQueueEnqueueBuffer(aq, b, 0, NULL);
}
static void s_close(Sink *s) {
    if (!s->q)
        return;
    AudioQueueRef dq = s->q;
    s->q = NULL; // async: Stop/Dispose can block like Start
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
      AudioQueueStop(dq, true);
      AudioQueueDispose(dq, true);
    });
    pthread_mutex_lock(&s->mu);
    free(s->ring);
    s->ring = NULL;
    s->ring_n = 0;
    pthread_mutex_unlock(&s->mu);
}
static int s_open(Sink *s, int r, int c) {
    s_close(s);
    AudioStreamBasicDescription f = {0};
    f.mSampleRate = r;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    f.mBytesPerPacket = f.mBytesPerFrame = 2 * c;
    f.mFramesPerPacket = 1;
    f.mChannelsPerFrame = c;
    f.mBitsPerChannel = 16;
    AudioQueueRef q;
    if (AudioQueueNewOutput(&f, fill, s, NULL, NULL, 0, &q))
        return -1;
    s->q = q;
    s->rate = r;
    s->chans = c;
    s->ring_cap = r * 2;
    s->ring = malloc(s->ring_cap * c * sizeof(short));
    s->ring_r = s->ring_n = 0;
    s->paused = 0;
    for (int i = 0; i < NBUF; i++) {
        AudioQueueAllocateBuffer(q, BUF_FRAMES * 2 * c, &s->bufs[i]);
        memset(s->bufs[i]->mAudioData, 0, BUF_FRAMES * 2 * c);
        s->bufs[i]->mAudioDataByteSize = BUF_FRAMES * 2 * c;
        AudioQueueEnqueueBuffer(q, s->bufs[i], 0, NULL);
    }
    // AudioQueueStart can block indefinitely when no output device is available (sandboxes, headless), so never run it on the caller's thread
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
      AudioQueueStart(q, NULL);
    });
    return 0;
}
static void s_push(Sink *s, const short *p, int n) {
    if (!s->q)
        return;
    pthread_mutex_lock(&s->mu);
    if (n > s->ring_cap) {
        p += (n - s->ring_cap) * s->chans;
        n = s->ring_cap;
    }
    if (s->ring_n + n > s->ring_cap) {
        int d = s->ring_n + n - s->ring_cap;
        s->ring_r = (s->ring_r + d) % s->ring_cap;
        s->ring_n -= d;
    } // overflow: drop oldest
    int w = (s->ring_r + s->ring_n) % s->ring_cap, a = s->ring_cap - w;
    if (a > n)
        a = n;
    memcpy(s->ring + w * s->chans, p, a * s->chans * sizeof(short));
    memcpy(s->ring, p + a * s->chans, (n - a) * s->chans * sizeof(short));
    s->ring_n += n;
    pthread_mutex_unlock(&s->mu);
}
int ha_open(int r, int c) {
    return s_open(&sinks[0], r, c);
}
void ha_push(const short *s, int n) {
    s_push(&sinks[0], s, n);
}
void ha_flush(void) {
    pthread_mutex_lock(&sinks[0].mu);
    sinks[0].ring_r = sinks[0].ring_n = 0;
    pthread_mutex_unlock(&sinks[0].mu);
}
void ha_set_gain(float g) {
    pthread_mutex_lock(&sinks[0].mu);
    sinks[0].gain = g;
    pthread_mutex_unlock(&sinks[0].mu);
}
void ha_pause(int p) {
    pthread_mutex_lock(&sinks[0].mu);
    sinks[0].paused = p;
    pthread_mutex_unlock(&sinks[0].mu);
}
void ha_close(void) {
    s_close(&sinks[0]);
}
int ha2_open(int r, int c) {
    return s_open(&sinks[1], r, c);
}
void ha2_push(const short *s, int n) {
    s_push(&sinks[1], s, n);
}
