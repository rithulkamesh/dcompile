// audio.h: the CoreAudio / AudioToolbox AUGraph surface the game's sound engine renders through.
//
// The graph's render callback is invoked at real-time pace on a helper guest thread (sched.h). After every callback the mixed
// buffers are converted to 16-bit stereo and pushed to a host audio queue (host/hostaudio.m), so game audio plays.

#include "host/hostaudio.h"
static struct {
    double rate;
    u32 fmt_flags, bytes_per_frame, channels, bits;
    int set;
} afmt;
static u32 audio_ioData, audio_flags, audio_ts, audio_frames = 512;
static struct {
    u32 fn, ref;
} au_cb;
static int nau_nodes;

H(NewAUGraph) {
    wr(A(0), 0x41000, 4);
    RET(0);
    return 0;
}
H(AUGraphAddNode) {
    wr(A(2), ++nau_nodes, 4);
    RET(0);
    return 0;
}
H(AUGraphNodeInfo) {
    if (A(3))
        wr(A(3), 0x42000 + A(1), 4);
    RET(0);
    return 0;
}
H(AUGraphSetNodeInputCallback) {
    au_cb.fn = rd(A(3), 4);
    au_cb.ref = rd(A(3) + 4, 4);
    RET(0);
    return 0;
}
H(AUGraphStart) {
    if (!au_cb.fn) {
        RET(0);
        return 0;
    }
    u32 chans = afmt.set ? afmt.channels : 2, bpf = afmt.set ? afmt.bytes_per_frame : 8;
    int nonint = afmt.set && (afmt.fmt_flags & 0x20);
    u32 nbuf = nonint ? chans : 1, bytes = audio_frames * (nonint ? bpf : bpf);
    audio_ioData = hmalloc(4 + 12 * nbuf);
    wr(audio_ioData, nbuf, 4);
    for (u32 i = 0; i < nbuf; i++) {
        u32 b = hmalloc(bytes);
        memset(P(b), 0, bytes);
        wr(audio_ioData + 4 + 12 * i, nonint ? 1 : chans, 4);
        wr(audio_ioData + 8 + 12 * i, bytes, 4);
        wr(audio_ioData + 12 + 12 * i, b, 4);
    }
    audio_flags = hmalloc(4);
    wr(audio_flags, 0, 4);
    audio_ts = hmalloc(64);
    memset(P(audio_ts), 0, 64);
    audio_cb_fn = au_cb.fn;
    u32 a[6] = {au_cb.ref, audio_flags, audio_ts, 0, audio_frames, audio_ioData};
    memcpy(audio_cb_args, a, sizeof a);
    audio_period_ns = (u64)(1e9 * audio_frames / (afmt.set && afmt.rate > 0 ? afmt.rate : 44100.0));
    audio_next_ns = mono_ns();
    audio_running = 1;
    RET(0);
    return 0;
}
H(AUGraphStop) {
    audio_running = 0;
    RET(0);
    return 0;
}
H(AudioUnitSetProperty) {                  // (unit, propID, scope, element, data, size)
    if (A(1) == 8 && A(4) && A(5) >= 36) { // kAudioUnitProperty_StreamFormat
        u64 q = (u64)rd(A(4), 4) | (u64)rd(A(4) + 4, 4) << 32;
        memcpy(&afmt.rate, &q, 8);
        afmt.fmt_flags = rd(A(4) + 12, 4);
        afmt.bytes_per_frame = rd(A(4) + 24, 4);
        afmt.channels = rd(A(4) + 28, 4);
        afmt.bits = rd(A(4) + 32, 4);
        afmt.set = 1;
    }
    RET(0);
    return 0;
}
H(AudioUnitGetProperty) { // (unit, propID, scope, element, data, size *)
    u32 d = A(4), sz = A(5) ? rd(A(5), 4) : 0;
    if (d && sz)
        memset(P(d), 0, sz);
    if (A(1) == 8 && d && sz >= 36) { // default: 44.1kHz, float32, 2ch, interleaved
        double r = 44100;
        memcpy(P(d), &r, 8);
        wr(d + 8, 0x6c70636d, 4);
        wr(d + 12, 0x29, 4);
        wr(d + 16, 8, 4);
        wr(d + 20, 1, 4);
        wr(d + 24, 8, 4);
        wr(d + 28, 2, 4);
        wr(d + 32, 32, 4);
    }
    RET(0);
    return 0;
}
// AudioObjectGetPropertyData[Size](object, AudioObjectPropertyAddress *{selector, scope, element}, qualSize, qual, ...):
// 'dOut' = default output device (id 1); anything else is answered as a stereo AudioChannelLayout (tag 0 = explicit descriptions).
#define AO_SEL_DEFAULT_OUT 0x644f7574u
H(AudioObjectGetPropertyDataSize) {
    wr(A(4), rd(A(1), 4) == AO_SEL_DEFAULT_OUT ? 4 : 12 + 2 * 20, 4);
    RET(0);
    return 0;
}
H(AudioObjectGetPropertyData) { // (obj, addr, qualSize, qual, UInt32 *ioSize, void *out)
    u32 out = A(5), n = rd(A(4), 4);
    memset(P(out), 0, n);
    if (rd(A(1), 4) == AO_SEL_DEFAULT_OUT) {
        wr(out, 1, 4);
        wr(A(4), 4, 4);
    } else if (n >= 52) {
        wr(out + 8, 2, 4);
        wr(out + 12, 1, 4);
        wr(out + 32, 2, 4);
        wr(A(4), 52, 4);
    } // channel descriptions: Left (1), Right (2)
    RET(0);
    return 0;
}
static void audio_zero(void) {
    if (!audio_ioData)
        return;
    for (u32 i = 0; i < rd(audio_ioData, 4); i++)
        memset(P(rd(audio_ioData + 12 + 12 * i, 4)), 0, rd(audio_ioData + 8 + 12 * i, 4));
}
static void audio_collect(
    void) { // the game's mixer just filled audio_ioData: convert to stereo s16 and hand it to the host output
    if (!audio_ioData || DBG_FLAG("NOGAMEAUDIO"))
        return;
    static int opened;
    u32 chans = afmt.set ? afmt.channels : 2;
    int nonint = afmt.set && (afmt.fmt_flags & 0x20), isfloat = !afmt.set || (afmt.fmt_flags & 1),
        bps = afmt.set ? afmt.bits / 8 : 4;
    if (!opened) {
        opened = 1;
        ha2_open((int)(afmt.set && afmt.rate > 0 ? afmt.rate : 44100), 2);
    }
    static short out[4096 * 2];
    u32 n = audio_frames < 4096 ? audio_frames : 4096;
    if (!chans)
        chans = 2;
    double peak = 0;
    for (u32 j = 0; j < n; j++)
        for (u32 ch = 0; ch < 2; ch++) {
            u32 src = ch < chans ? ch : 0, addr = nonint ? rd(audio_ioData + 12 + 12 * src, 4) + j * bps
                                                         : rd(audio_ioData + 12, 4) + (j * chans + src) * bps;
            double v;
            if (isfloat && bps == 4) {
                u32 w = rd(addr, 4);
                float f;
                memcpy(&f, &w, 4);
                v = f;
            } else if (bps == 2)
                v = (i16)rd(addr, 2) / 32768.0;
            else
                v = (i32)rd(addr, 4) / 2147483648.0;
            if (v > 1)
                v = 1;
            if (v < -1)
                v = -1;
            if (fabs(v) > peak)
                peak = fabs(v);
            out[j * 2 + ch] = (short)(v * 32767);
        }
    ha2_push(out, (int)n);
    static u64 cnt;
    static double pk;
    if (peak > pk)
        pk = peak;
    if (DBG_FLAG("AUDIOLOG") && ++cnt % 200 == 0) {
        fprintf(stderr, "[audio] %llu callbacks, peak %.3f, fmt flags %x bits %u ch %u nonint %d\n",
                (unsigned long long)cnt, pk, afmt.fmt_flags, afmt.bits, afmt.channels, nonint);
        pk = 0;
    }
}
H(au_ok) {
    RET(0);
    return 0;
}

// Imports this module provides (see runtime/imports.h).
// clang-format off
static const ImportEntry audio_imports[] = {
    IMPORT(NewAUGraph), IMPORT(AUGraphAddNode), IMPORT(AUGraphNodeInfo),
    IMPORT(AUGraphSetNodeInputCallback), IMPORT(AUGraphStart), IMPORT(AUGraphStop),
    IMPORT(AudioUnitSetProperty), IMPORT(AudioUnitGetProperty), IMPORT(AudioObjectGetPropertyDataSize),
    IMPORT(AudioObjectGetPropertyData), {"_AUGraphOpen", h_au_ok}, {"_AUGraphClose", h_au_ok},
    {"_AUGraphInitialize", h_au_ok}, {"_AUGraphConnectNodeInput", h_au_ok}, {"_DisposeAUGraph", h_au_ok},
    {"_AudioOutputUnitStop", h_au_ok}, {"_AudioUnitUninitialize", h_au_ok},
    {0, 0}};
// clang-format on
