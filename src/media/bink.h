// bink.h: the RAD Bink video API (the 14 imports of libBinkMachOx86.dylib) implemented on top of ffmpeg.
//
// HBINK is a 0x100-byte guest struct; the game reads Frames (+8), FrameNum (+0xc, 1-based) and FrameRate/FrameRateDiv
// (+0x14/+0x18) directly, everything else goes through the API. Video is decoded by libavcodec on the host, converted with
// swscale and uploaded as textures; movie audio is played through host/hostaudio.m.

#pragma push_macro("A")
#pragma push_macro("P")
#pragma push_macro("H")
#pragma push_macro("RET")
#undef A
#undef P
#undef H
#undef RET
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#pragma pop_macro("A")
#pragma pop_macro("P")
#pragma pop_macro("H")
#pragma pop_macro("RET")
#include "host/hostaudio.h"

#define BK_MAX 8
#define BK_AREF 48000
typedef struct {
    u32 g; // guest HBINK, 0 = free slot
    AVFormatContext *fmt;
    AVCodecContext *vc, *ac;
    int vi, ai;
    AVPacket *pkt;
    AVFrame *vf, *af;
    struct SwsContext *sws;
    struct SwrContext *swr;
    int w, h, frames, decoded; // decoded: 1-based index of the frame in vf (0 = none)
    u32 rate, div;             // fps = rate / div
    uint8_t *bgra;
    int bgra_for; // vf converted to BGRA (cache), valid when bgra_for == decoded
    u64 start_ns, pause_ns;
    int started, paused, eof;
} BK;
static BK bks[BK_MAX];
static u32 bk_tracks[8];
static int bk_ntracks;
static int bk_audio_on;
static int bk_log = -1;
#define BKLOG(...)                                                                                           \
    do {                                                                                                     \
        if (bk_log < 0)                                                                                      \
            bk_log = dbg_env("BINKLOG") != NULL;                                                             \
        if (bk_log)                                                                                          \
            fprintf(stderr, "[bink] " __VA_ARGS__);                                                          \
    } while (0)

static BK *bk_get(u32 g) {
    for (int i = 0; i < BK_MAX; i++)
        if (bks[i].g && bks[i].g == g)
            return &bks[i];
    return NULL;
}
static u64 bk_dur_ns(BK *b) {
    return 1000000000ull * b->div / (b->rate ? b->rate : 30);
}
static u64 bk_due(BK *b, int frame) {
    return b->start_ns + (u64)(frame - 1) * bk_dur_ns(b);
}
static u64 bk_now(BK *b) {
    return b->paused ? b->pause_ns : mono_ns();
}

static void bk_audio_push(BK *b, AVFrame *f) {
    if (!b->swr) {
        AVChannelLayout out = AV_CHANNEL_LAYOUT_STEREO;
        if (swr_alloc_set_opts2(&b->swr, &out, AV_SAMPLE_FMT_S16, BK_AREF, &f->ch_layout, f->format,
                                f->sample_rate, 0, NULL) ||
            swr_init(b->swr)) {
            swr_free(&b->swr);
            b->ai = -1;
            return;
        }
    }
    int max = swr_get_out_samples(b->swr, f->nb_samples);
    short *buf = malloc((size_t)max * 4 + 64);
    uint8_t *o[1] = {(uint8_t *)buf};
    int n = swr_convert(b->swr, o, max, (const uint8_t **)f->extended_data, f->nb_samples);
    if (n > 0 && bk_audio_on)
        ha_push(buf, n);
    free(buf);
}

// Reads packets until frame `target` (1-based) is decoded into b->vf (or EOF). Audio of the selected track is decoded on the way.
static void bk_decode_to(BK *b, int target) {
    while (b->decoded < target && !b->eof) {
        int r = av_read_frame(b->fmt, b->pkt);
        if (r < 0) {
            b->eof = 1;
            break;
        }
        if (b->pkt->stream_index == b->vi) {
            if (avcodec_send_packet(b->vc, b->pkt) >= 0)
                if (avcodec_receive_frame(b->vc, b->vf) >= 0)
                    b->decoded++; // one frame per packet; a second receive would unref vf
        } else if (b->pkt->stream_index == b->ai && b->ac) {
            if (avcodec_send_packet(b->ac, b->pkt) >= 0)
                while (avcodec_receive_frame(b->ac, b->af) >= 0)
                    bk_audio_push(b, b->af);
        }
        av_packet_unref(b->pkt);
    }
}

H(BinkSetMemory) {
    RET(1);
    return 0;
} // (alloc, free): ffmpeg uses the host heap
H(BinkSetSoundTrack) { // (n, u32 *ids)
    bk_ntracks = (int)A(0) > 8 ? 8 : (int)A(0);
    for (int i = 0; i < bk_ntracks; i++)
        bk_tracks[i] = rd(A(1) + 4 * i, 4);
    RET(1);
    return 0;
}
H(BinkOpen) { // (path, flags) -> HBINK
    const char *path = GS(A(0));
    BK *b = NULL;
    for (int i = 0; i < BK_MAX; i++)
        if (!bks[i].g) {
            b = &bks[i];
            break;
        }
    if (!b) {
        RET(0);
        return 0;
    }
    memset(b, 0, sizeof *b);
    b->ai = -1;
    int ok = avformat_open_input(&b->fmt, path, NULL, NULL) >= 0;
    if (!ok) { // the game probes a language directory we may not have (it asks for videos/ko/...): retry under en/
        char alt[1024], *sl = strrchr(path, '/');
        const char *file = sl ? sl + 1 : path;
        if (sl) {
            snprintf(alt, sizeof alt, "%.*s", (int)(sl - path), path);
            char *d = strrchr(alt, '/');
            if (d) {
                snprintf(d, alt + sizeof alt - d, "/en/%s", file);
                ok = avformat_open_input(&b->fmt, alt, NULL, NULL) >= 0;
                if (ok)
                    BKLOG("%s -> %s\n", path, alt);
            }
        }
    }
    if (!ok) {
        BKLOG("open %s failed\n", path);
        RET(0);
        return 0;
    }
    b->vi = av_find_best_stream(b->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (b->vi < 0)
        goto fail;
    AVStream *vs = b->fmt->streams[b->vi];
    const AVCodec *vcodec = avcodec_find_decoder(vs->codecpar->codec_id);
    b->vc = avcodec_alloc_context3(vcodec);
    if (!vcodec || avcodec_parameters_to_context(b->vc, vs->codecpar) < 0 ||
        avcodec_open2(b->vc, vcodec, NULL) < 0)
        goto fail;
    b->w = vs->codecpar->width;
    b->h = vs->codecpar->height;
    AVRational fr = vs->avg_frame_rate.num ? vs->avg_frame_rate : av_make_q(30, 1);
    b->rate = fr.num;
    b->div = fr.den;
    b->frames = vs->nb_frames      ? (int)vs->nb_frames
                : vs->duration > 0 ? (int)av_rescale_q(vs->duration, vs->time_base, av_inv_q(fr))
                                   : (int)(b->fmt->duration * fr.num / fr.den / AV_TIME_BASE);
    // audio: the demuxer exposes every Bink track as its own stream whose id is the Bink track id; BinkSetSoundTrack picks one (default: first)
    for (int i = 0; i < (int)b->fmt->nb_streams; i++) {
        AVStream *s = b->fmt->streams[i];
        if (i == b->vi)
            continue;
        if (s->codecpar->codec_type != AVMEDIA_TYPE_AUDIO || b->ai >= 0 ||
            (bk_ntracks && (u32)s->id != bk_tracks[0]))
            s->discard = AVDISCARD_ALL;
        else
            b->ai = i;
    }
    if (b->ai >= 0) {
        AVStream *as = b->fmt->streams[b->ai];
        const AVCodec *ac = avcodec_find_decoder(as->codecpar->codec_id);
        b->ac = avcodec_alloc_context3(ac);
        if (!ac || avcodec_parameters_to_context(b->ac, as->codecpar) < 0 ||
            avcodec_open2(b->ac, ac, NULL) < 0) {
            avcodec_free_context(&b->ac);
            b->fmt->streams[b->ai]->discard = AVDISCARD_ALL;
            b->ai = -1;
        }
    }
    b->pkt = av_packet_alloc();
    b->vf = av_frame_alloc();
    b->af = av_frame_alloc();
    b->g = hmalloc(0x100);
    memset(P(b->g), 0, 0x100);
    wr(b->g, b->w, 4);
    wr(b->g + 4, b->h, 4);
    wr(b->g + 8, b->frames, 4);
    wr(b->g + 0xc, 1, 4);
    wr(b->g + 0x14, b->rate, 4);
    wr(b->g + 0x18, b->div, 4);
    wr(b->g + 0x20, A(1), 4);
    bk_audio_on = b->ai >= 0 && ha_open(BK_AREF, 2) == 0;
    BKLOG("[flip %llu] open %s %dx%d frames=%d fps=%u/%u audio stream=%d (track ids:%d first=%u)\n",
          (unsigned long long)nflips, path, b->w, b->h, b->frames, b->rate, b->div, b->ai, bk_ntracks,
          bk_tracks[0]);
    RET(b->g);
    return 0;
fail:
    BKLOG("open %s: unsupported stream\n", path);
    avcodec_free_context(&b->vc);
    avformat_close_input(&b->fmt);
    memset(b, 0, sizeof *b);
    RET(0);
    return 0;
}
H(BinkClose) {
    BK *b = bk_get(A(0));
    if (b) {
        BKLOG("[flip %llu] close at frame %u/%d\n", (unsigned long long)nflips, rd(b->g + 0xc, 4), b->frames);
        if (bk_audio_on)
            ha_close();
        bk_audio_on = 0;
        av_frame_free(&b->vf);
        av_frame_free(&b->af);
        av_packet_free(&b->pkt);
        sws_freeContext(b->sws);
        swr_free(&b->swr);
        avcodec_free_context(&b->vc);
        avcodec_free_context(&b->ac);
        avformat_close_input(&b->fmt);
        free(b->bgra);
        memset(b, 0, sizeof *b);
    }
    RET(0);
    return 0;
}
H(BinkDoFrame) {
    BK *b = bk_get(A(0));
    if (b)
        bk_decode_to(b, (int)rd(b->g + 0xc, 4));
    RET(0);
    return 0;
}
H(BinkNextFrame) { // advances; stays on the last frame
    BK *b = bk_get(A(0));
    if (b) {
        u32 n = rd(b->g + 0xc, 4);
        if ((int)n < b->frames)
            wr(b->g + 0xc, n + 1, 4);
        wr(b->g + 0x10, n, 4);
    }
    return 0;
}
H(BinkWait) { // nonzero = the current frame is not due yet
    BK *b = bk_get(A(0));
    if (!b) {
        RET(0);
        return 0;
    }
    if (!b->started) {
        b->started = 1;
        b->start_ns = mono_ns();
    }
    RET(!b->paused && mono_ns() < bk_due(b, (int)rd(b->g + 0xc, 4)));
    return 0;
}
H(BinkShouldSkip) { // nonzero = already late for the following frame
    BK *b = bk_get(A(0));
    u32 n = b ? rd(b->g + 0xc, 4) : 0;
    RET(b && b->started && !b->paused && (int)n < b->frames && mono_ns() >= bk_due(b, n + 1));
    return 0;
}
H(BinkPause) {
    BK *b = bk_get(A(0));
    int p = (int)A(1) != 0;
    if (b && p != b->paused) {
        if (p)
            b->pause_ns = mono_ns();
        else
            b->start_ns += mono_ns() - b->pause_ns;
        b->paused = p;
        if (bk_audio_on)
            ha_pause(p);
    }
    RET(p);
    return 0;
}
H(BinkGoto) { // (bink, frame, flags)
    BK *b = bk_get(A(0));
    int f = (int)A(1);
    if (b) {
        if (f < 1)
            f = 1;
        if (f > b->frames)
            f = b->frames;
        if (f <= b->decoded) { // rewind: Bink is inter-coded, so restart from the first frame
            av_seek_frame(b->fmt, b->vi, 0, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(b->vc);
            if (b->ac)
                avcodec_flush_buffers(b->ac);
            b->decoded = 0;
            b->eof = 0;
            if (bk_audio_on)
                ha_flush();
        }
        wr(b->g + 0xc, f, 4);
        b->start_ns = bk_now(b) - (u64)(f - 1) * bk_dur_ns(b);
    }
    RET(0);
    return 0;
}
H(BinkSetVolume) {
    BK *b = bk_get(A(0));
    u32 v = A(2);
    if (b && bk_audio_on && !(bk_ntracks && A(1) != bk_tracks[0]))
        ha_set_gain(v / 32768.0f);
    RET(1);
    return 0;
}

// BINKSURFACE32 3 (BGRX), 32R 4 (RGBX), 32A 5 (BGRA), 32RA 6 (RGBA); BINKCOPYALL (0x80000000) is implied: we always write the full rect.
static int bk_pixfmt(u32 flags) {
    switch (flags & 0xff) {
    case 3:
    case 5:
        return AV_PIX_FMT_BGRA;
    case 4:
    case 6:
        return AV_PIX_FMT_RGBA;
    }
    return -1;
}
static void bk_copy(BK *b, u32 dst, u32 pitch, u32 dh, u32 dx, u32 dy, u32 sx, u32 sy, u32 sw, u32 sh,
                    u32 flags) {
    int pf = bk_pixfmt(flags);
    if (pf < 0) {
        BKLOG("unsupported surface format %u\n", flags);
        return;
    }
    if (!b->decoded || !b->vf->data[0]) {
        BKLOG("copy: nothing decoded (%d)\n", b->decoded);
        return;
    }
    if (sx >= (u32)b->w || sy >= (u32)b->h)
        return;
    if (sx + sw > (u32)b->w)
        sw = b->w - sx;
    if (sy + sh > (u32)b->h)
        sh = b->h - sy;
    if (dy >= dh)
        return;
    if (dy + sh > dh)
        sh = dh - dy;
    // convert the whole frame once per (frame, format) into a cache, then blit the rect
    static int cache_fmt = -1;
    if (b->bgra_for != b->decoded || cache_fmt != pf) {
        if (!b->bgra)
            b->bgra = malloc((size_t)b->w * b->h * 4);
        b->sws = sws_getCachedContext(b->sws, b->w, b->h, b->vf->format, b->w, b->h, pf, SWS_BILINEAR, NULL,
                                      NULL, NULL);
        uint8_t *d[4] = {b->bgra};
        int ls[4] = {b->w * 4};
        if (b->vf->color_range == AVCOL_RANGE_UNSPECIFIED || b->vf->color_range == AVCOL_RANGE_MPEG) {
            const int *tab =
                sws_getCoefficients(b->vf->colorspace == AVCOL_SPC_BT709 ? SWS_CS_ITU709 : SWS_CS_ITU601);
            sws_setColorspaceDetails(b->sws, tab, 0, tab, 0, 0, 1 << 16, 1 << 16);
        }
        sws_scale(b->sws, (const uint8_t *const *)b->vf->data, b->vf->linesize, 0, b->h, d, ls);
        if (!(av_pix_fmt_desc_get(b->vf->format)->flags & AV_PIX_FMT_FLAG_ALPHA))
            for (size_t i = 3; i < (size_t)b->w * b->h * 4; i += 4)
                b->bgra[i] = 0xff;
        b->bgra_for = b->decoded;
        cache_fmt = pf;
    }
    for (u32 y = 0; y < sh; y++) {
        u32 n = sw;
        if (dx + n > pitch / 4)
            n = dx < pitch / 4 ? pitch / 4 - dx : 0;
        if (n)
            memcpy(P(dst + (dy + y) * pitch + dx * 4), b->bgra + ((size_t)(sy + y) * b->w + sx) * 4, n * 4);
    }
}
H(BinkCopyToBuffer) { // (bink, dest, pitch, destheight, destx, desty, flags)
    BK *b = bk_get(A(0));
    if (b)
        bk_copy(b, A(1), A(2), A(3), A(4), A(5), 0, 0, b->w, b->h, A(6));
    RET(0);
    return 0;
}
H(BinkCopyToBufferRect) { // (bink, dest, pitch, destheight, destx, desty, srcx, srcy, srcw, srch, flags)
    BK *b = bk_get(A(0));
    if (b)
        bk_copy(b, A(1), A(2), A(3), A(4), A(5), A(6), A(7), A(8), A(9), A(10));
    RET(0);
    return 0;
}
H(BinkGetFrameBuffersInfo) { // (bink, BINKFRAMEBUFFERS *out): TotalFrames, YABuffer w/h, cRcBBuffer w/h, FrameNum, then plane sets (zero)
    BK *b = bk_get(A(0));
    u32 o = A(1);
    if (b && o) {
        memset(P(o), 0, 0x18 + 2 * 4 * 12);
        wr(o, 2, 4);
        wr(o + 4, b->w, 4);
        wr(o + 8, b->h, 4);
        wr(o + 12, (b->w + 1) / 2, 4);
        wr(o + 16, (b->h + 1) / 2, 4);
    }
    RET(0);
    return 0;
}

// Imports this module provides (see runtime/imports.h).
// clang-format off
static const ImportEntry bink_imports[] = {
    IMPORT(BinkSetMemory), IMPORT(BinkOpen), IMPORT(BinkClose), IMPORT(BinkDoFrame), IMPORT(BinkNextFrame),
    IMPORT(BinkWait), IMPORT(BinkGoto), IMPORT(BinkPause), IMPORT(BinkCopyToBuffer),
    IMPORT(BinkCopyToBufferRect), IMPORT(BinkGetFrameBuffersInfo), IMPORT(BinkSetSoundTrack),
    IMPORT(BinkSetVolume), IMPORT(BinkShouldSkip),
    {0, 0}};
// clang-format on
