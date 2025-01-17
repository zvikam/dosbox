/*
 * Copyright (c) 2003 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * libavformat API example.
 *
 * Output a media file in any supported libavformat format. The default
 * codecs are used.
 * @example muxing.c
 */

#include "config.h"

#if(C_STREAM)

#define __STDC_CONSTANT_MACROS

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "muxing.h"

#ifndef max
#define max(a,b) ((a)>(b)?(a):(b))
#endif
#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif

#define LOG_MSG printf

#define SCALE_FLAGS SWS_BICUBIC

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    LOG_MSG("pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
}

static int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt)
{
    /* rescale output packet timestamp values from codec to stream timebase */
    av_packet_rescale_ts(pkt, *time_base, st->time_base);
    pkt->stream_index = st->index;

    /* Write the compressed frame to the media file. */
    //log_packet(fmt_ctx, pkt);
    return av_interleaved_write_frame(fmt_ctx, pkt);
}

/* Add an output stream. */
static int add_stream(OutputStream *ost, AVFormatContext *oc,
                       AVCodec **codec,
                       enum AVCodecID codec_id,
                       StreamContext* ctx)
{
    AVCodecContext *c;
    int i;

    /* find the encoder */
    *codec = avcodec_find_encoder(codec_id);
    if (!(*codec)) {
        LOG_MSG("Could not find encoder for '%s'\n",
                avcodec_get_name(codec_id));
        return -1;
    }

    ost->st = avformat_new_stream(oc, NULL);
    if (!ost->st) {
        LOG_MSG("Could not allocate stream\n");
        return -1;
    }
    ost->st->id = oc->nb_streams-1;
    c = avcodec_alloc_context3(*codec);
    if (!c) {
        LOG_MSG("Could not alloc an encoding context\n");
        return -1;
    }
    ost->enc = c;

    ost->pkt = av_packet_alloc();
    if (!ost->pkt) {
        LOG_MSG("Could not alloc a packet\n");
        return -1;
    }

    switch ((*codec)->type) {
    case AVMEDIA_TYPE_AUDIO:
        c->sample_fmt  = (*codec)->sample_fmts ?
            (*codec)->sample_fmts[0] : AV_SAMPLE_FMT_S16;
        c->bit_rate    = 64000;
        c->sample_rate = 44100;
        if ((*codec)->supported_samplerates) {
            c->sample_rate = (*codec)->supported_samplerates[0];
            for (i = 0; (*codec)->supported_samplerates[i]; i++) {
                if ((*codec)->supported_samplerates[i] == 44100)
                    c->sample_rate = 44100;
            }
        }
        c->channel_layout = AV_CH_LAYOUT_STEREO;
        if ((*codec)->channel_layouts) {
            c->channel_layout = (*codec)->channel_layouts[0];
            for (i = 0; (*codec)->channel_layouts[i]; i++) {
                if ((*codec)->channel_layouts[i] == AV_CH_LAYOUT_STEREO)
                    c->channel_layout = AV_CH_LAYOUT_STEREO;
            }
        }
        c->channels        = av_get_channel_layout_nb_channels(c->channel_layout);
        ost->st->time_base = (AVRational){ 1, c->sample_rate };
        break;

    case AVMEDIA_TYPE_VIDEO:
        c->codec_id = codec_id;

        c->bit_rate = 750000;
        /* Resolution must be a multiple of two. */
        c->width    = ctx->width;
        c->height   = ctx->height;
        /* timebase: This is the fundamental unit of time (in seconds) in terms
         * of which frame timestamps are represented. For fixed-fps content,
         * timebase should be 1/framerate and timestamp increments should be
         * identical to 1. */
        ost->st->time_base = (AVRational){ 1, ctx->fps };
        c->time_base       = ost->st->time_base;

        c->gop_size      = 12; /* emit one intra frame every twelve frames at most */
        c->pix_fmt       = AV_PIX_FMT_YUV420P;
        if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
            /* just for testing, we also add B-frames */
            c->max_b_frames = 2;
        }
        if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
            /* Needed to avoid using macroblocks in which some coeffs overflow.
             * This does not happen with normal video, it just happens here as
             * the motion of the chroma plane does not match the luma plane. */
            c->mb_decision = 2;
        }
    break;

    default:
        break;
    }

    /* Some formats want stream headers to be separate. */
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    return 0;
}

/**************************************************************/
/* audio output */

static AVFrame *alloc_audio_frame(enum AVSampleFormat sample_fmt,
                                  uint64_t channel_layout,
                                  int sample_rate, int nb_samples)
{
    AVFrame *frame = av_frame_alloc();
    int ret;

    if (!frame) {
        LOG_MSG("Error allocating an audio frame\n");
        return NULL;
    }

    frame->format = sample_fmt;
    frame->channel_layout = channel_layout;
    frame->sample_rate = sample_rate;
    frame->nb_samples = nb_samples;

    if (nb_samples) {
        ret = av_frame_get_buffer(frame, 0);
        if (ret < 0) {
            LOG_MSG("Error allocating an audio buffer\n");
            return NULL;
        }
    }

    return frame;
}

static int open_audio(AVFormatContext *oc, AVCodec *codec, OutputStream *ost, AVDictionary *opt_arg)
{
    AVCodecContext *c;
    int nb_samples;
    int ret;
    AVDictionary *opt = NULL;

    c = ost->enc;

    /* open it */
    av_dict_copy(&opt, opt_arg, 0);
    ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        LOG_MSG("Could not open audio codec: %s\n", av_err2str(ret));
        return ret;
    }

    if (c->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE)
        nb_samples = 10000;
    else
        nb_samples = c->frame_size;

    ost->frame     = alloc_audio_frame(c->sample_fmt, c->channel_layout,
                                       c->sample_rate, nb_samples);
    ost->tmp_frame = alloc_audio_frame(AV_SAMPLE_FMT_S16, c->channel_layout,
                                       c->sample_rate, nb_samples);

    /* copy the stream parameters to the muxer */
    ret = avcodec_parameters_from_context(ost->st->codecpar, c);
    if (ret < 0) {
        LOG_MSG("Could not copy the stream parameters\n");
        return ret;
    }

    /* create resampler context */
    ost->swr_ctx = swr_alloc();
    if (!ost->swr_ctx) {
        LOG_MSG("Could not allocate resampler context\n");
        return -1;
    }

    /* set options */
    av_opt_set_int       (ost->swr_ctx, "in_channel_count",   c->channels,       0);
    av_opt_set_int       (ost->swr_ctx, "in_sample_rate",     c->sample_rate,    0);
    av_opt_set_sample_fmt(ost->swr_ctx, "in_sample_fmt",      AV_SAMPLE_FMT_S16, 0);
    av_opt_set_int       (ost->swr_ctx, "out_channel_count",  c->channels,       0);
    av_opt_set_int       (ost->swr_ctx, "out_sample_rate",    c->sample_rate,    0);
    av_opt_set_sample_fmt(ost->swr_ctx, "out_sample_fmt",     c->sample_fmt,     0);

    /* initialize the resampling context */
    if ((ret = swr_init(ost->swr_ctx)) < 0) {
        LOG_MSG("Failed to initialize the resampling context\n");
        return ret;
    }

    return 0;
}

/* Prepare a 16 bit dummy audio frame of 'frame_size' samples and
 * 'nb_channels' channels. */
static AVFrame *get_audio_frame(OutputStream *ost, int* nb_samples)
{
    AVFrame *frame = ost->tmp_frame;

    frame->pts = ost->next_pts;
    if (*nb_samples == 0)
        *nb_samples = frame->nb_samples;
    ost->next_pts  += *nb_samples;

    return frame;
}

/*
 * encode one audio frame and send it to the muxer
 * return 1 when encoding is finished, 0 otherwise, <0 on error
 */
static int write_audio_frame(AVFormatContext *oc, OutputStream *ost, int nb_samples)
{
    AVCodecContext *c;
    AVFrame *frame;
    int ret;
    int got_packet;
    int dst_nb_samples;

    c = ost->enc;

    frame = get_audio_frame(ost, &nb_samples);

    if (frame) {
        /* convert samples from native format to destination codec format, using the resampler */
            /* compute destination number of samples */
            dst_nb_samples = av_rescale_rnd(swr_get_delay(ost->swr_ctx, c->sample_rate) + nb_samples,
                                            c->sample_rate, c->sample_rate, AV_ROUND_UP);

        /* when we pass a frame to the encoder, it may keep a reference to it
         * internally;
         * make sure we do not overwrite it here
         */
        ret = av_frame_make_writable(ost->frame);
        if (ret < 0)
            return ret;

        /* convert to destination format */
        ret = swr_convert(ost->swr_ctx,
                          ost->frame->data, dst_nb_samples,
                          (const uint8_t **)frame->data, nb_samples);
        if (ret < 0) {
            LOG_MSG("Error while converting\n");
            return ret;
        }
        frame = ost->frame;

        frame->pts = av_rescale_q(ost->samples_count, (AVRational){1, c->sample_rate}, c->time_base);
        ost->samples_count += dst_nb_samples;
    }

    /* encode the image */
    ret = avcodec_send_frame(ost->enc, frame);
    if (ret < 0) {
        LOG_MSG("Error encoding audio frame: %s\n", av_err2str(ret));
        return ret;
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(ost->enc, ost->pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return 0;
        else if (ret < 0) {
            LOG_MSG("Error during encoding\n");
            return -1;
        }
        got_packet |= 1;
        ret = write_frame(oc, &c->time_base, ost->st, ost->pkt);
        av_packet_unref(ost->pkt);
        if (ret < 0) {
            LOG_MSG("Error while writing audio frame: %s\n", av_err2str(ret));
            return -1;
        }
        ret = 0;
    }

    return (frame || got_packet) ? 0 : 1;
}

/**************************************************************/
/* video output */

static AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height)
{
    AVFrame *picture;
    int ret;

    picture = av_frame_alloc();
    if (!picture)
        return NULL;

    picture->format = pix_fmt;
    picture->width  = width;
    picture->height = height;

    /* allocate the buffers for the frame data */
    ret = av_frame_get_buffer(picture, 32);
    if (ret < 0) {
        LOG_MSG("Could not allocate frame data.\n");
        return NULL;
    }

    return picture;
}

static int open_video(AVFormatContext *oc, AVCodec *codec, OutputStream *ost, AVDictionary *opt_arg)
{
    int ret;
    AVCodecContext *c = ost->enc;
    AVDictionary *opt = NULL;

    av_dict_copy(&opt, opt_arg, 0);

    av_opt_set(c->priv_data, "preset", "slow", 0);

    /* open the codec */
    ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        LOG_MSG("Could not open video codec: %s\n", av_err2str(ret));
        return ret;
    }

    /* allocate and init a re-usable frame */
    ost->frame = alloc_picture(c->pix_fmt, c->width, c->height);
    if (!ost->frame) {
        LOG_MSG("Could not allocate video frame\n");
        return -1;
    }

    ost->tmp_frame = NULL;
    ost->tmp_frame = alloc_picture(AV_PIX_FMT_RGB24, c->width, c->height);
    if (!ost->tmp_frame) {
        LOG_MSG("Could not allocate temporary picture\n");
        return -1;
    }

    /* copy the stream parameters to the muxer */
    ret = avcodec_parameters_from_context(ost->st->codecpar, c);
    if (ret < 0) {
        LOG_MSG("Could not copy the stream parameters\n");
        return ret;
    }

    return 0;
}

static AVFrame *get_video_frame(OutputStream *ost)
{
    AVCodecContext *c = ost->enc;

    /* when we pass a frame to the encoder, it may keep a reference to it
     * internally; make sure we do not overwrite it here */
    if (av_frame_make_writable(ost->frame) < 0)
        return NULL;

    /* as we only always get an RGB24 picture, we must convert it
     * to the codec pixel format if needed */
    if (!ost->sws_ctx) {
        ost->sws_ctx = sws_getContext(c->width, c->height,
                                        AV_PIX_FMT_RGB24,
                                        c->width, c->height,
                                        c->pix_fmt,
                                        SCALE_FLAGS, NULL, NULL, NULL);
        if (!ost->sws_ctx) {
            LOG_MSG(
                    "Could not initialize the conversion context\n");
            return NULL;
        }
    }
    sws_scale(ost->sws_ctx, (const uint8_t * const *) ost->tmp_frame->data,
                ost->tmp_frame->linesize, 0, c->height, ost->frame->data,
                ost->frame->linesize);

    ost->frame->pts = ost->next_pts++;

    return ost->frame;
}

/*
 * encode one video frame and send it to the muxer
 * return 1 when encoding is finished, 0 otherwise, <0 on error
 */
static int write_video_frame(AVFormatContext *oc, OutputStream *ost)
{
    int ret;
    AVCodecContext *c;
    AVFrame *frame;
    int got_packet = 0;

    c = ost->enc;

    frame = get_video_frame(ost);

    /* encode the image */
    ret = avcodec_send_frame(ost->enc, frame);
    if (ret < 0) {
        LOG_MSG("Error encoding video frame: %s\n", av_err2str(ret));
        return ret;
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(ost->enc, ost->pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return 0;
        else if (ret < 0) {
            LOG_MSG("Error during encoding\n");
            return -1;
        }
        got_packet |= 1;
        ret = write_frame(oc, &c->time_base, ost->st, ost->pkt);
        av_packet_unref(ost->pkt);
        if (ret < 0) {
            LOG_MSG("Error while writing video frame: %s\n", av_err2str(ret));
            return -1;
        }
        ret = 0;
    }

    return (frame || got_packet) ? 0 : 1;
}

static void close_stream(AVFormatContext *oc, OutputStream *ost)
{
    avcodec_free_context(&ost->enc);
    av_frame_free(&ost->frame);
    av_frame_free(&ost->tmp_frame);
    sws_freeContext(ost->sws_ctx);
    swr_free(&ost->swr_ctx);
    av_packet_free(&ost->pkt);

    ost->sws_ctx = NULL;
    ost->swr_ctx = NULL;
}

/**************************************************************/
int streaming_init(const char *streamname, StreamContext* ctx)
{
    AVOutputFormat *fmt;
    int ret;

    ctx->frames = 0;
    ctx->bufferedAudio = 0;

    avformat_alloc_output_context2(&ctx->oc, NULL, "flv", streamname);

    if (!ctx->oc)
        return 1;

    fmt = ctx->oc->oformat;

    /* Add the audio and video streams using the default format codecs
     * and initialize the codecs. */
    if (fmt->video_codec != AV_CODEC_ID_NONE) {
        add_stream(&ctx->video_st, ctx->oc, &ctx->video_codec, fmt->video_codec, ctx);
    }
    if (fmt->audio_codec != AV_CODEC_ID_NONE) {
        add_stream(&ctx->audio_st, ctx->oc, &ctx->audio_codec, fmt->audio_codec, ctx);
    }

    /* Now that all the parameters are set, we can open the audio and
     * video codecs and allocate the necessary encode buffers. */
    open_video(ctx->oc, ctx->video_codec, &ctx->video_st, NULL);
    open_audio(ctx->oc, ctx->audio_codec, &ctx->audio_st, NULL);

    av_dump_format(ctx->oc, 0, streamname, 1);

    /* open the output file, if needed */
    if (!(fmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ctx->oc->pb, streamname, AVIO_FLAG_WRITE);
        if (ret < 0) {
            LOG_MSG("Could not open '%s': %s\n", streamname,
                    av_err2str(ret));
            return 1;
        }
    }

    /* Write the stream header, if any. */
    ret = avformat_write_header(ctx->oc, NULL);
    if (ret < 0) {
        LOG_MSG("Error occurred when opening output file: %s\n",
                av_err2str(ret));
        return 1;
    }

    return 0;
}

int streaming_cleanup(StreamContext* ctx)
{
    /* Write the trailer, if any. The trailer must be written before you
     * close the CodecContexts open when you wrote the header; otherwise
     * av_write_trailer() may try to use memory that was freed on
     * av_codec_close(). */
    av_write_trailer(ctx->oc);

    /* Close each codec. */
    close_stream(ctx->oc, &ctx->video_st);
    close_stream(ctx->oc, &ctx->audio_st);

    /* free the stream */
    avformat_free_context(ctx->oc);

    ctx->oc = NULL;

    return 0;
}

int streaming_video_line(StreamContext* ctx, int y, Bit8u *data)
{
    AVFrame* pict = ctx->video_st.tmp_frame;
    memcpy(&pict->data[0][y * pict->linesize[0]], data, pict->width * 3);

    return 0;
}

int streaming_video(StreamContext* ctx)
{
    return write_video_frame(ctx->oc, &ctx->video_st);
}

int streaming_audio(StreamContext* ctx, Bit32u len, Bit16s *data)
{
    int ret = 0;
    len *= 4; // convert to bytes
    AVFrame *frame = ctx->audio_st.tmp_frame;
    if (frame != NULL) {
        int frameByfferSize = frame->nb_samples * 4; // convert to bytes
        int copyBytes = min(len, frameByfferSize - ctx->bufferedAudio);
        //LOG_MSG("buffering %u audio bytes\n", copyBytes);
        memcpy(&frame->data[0][ctx->bufferedAudio], data, copyBytes);
        ctx->bufferedAudio += copyBytes;

        {
            //LOG_MSG("using %u audio bytes\n", ctx->bufferedAudio);
            ret = write_audio_frame(ctx->oc, &ctx->audio_st, ctx->bufferedAudio / 4);

            ctx->bufferedAudio = 0;
            if (copyBytes < len) {
                //LOG_MSG("buffering %u left-over audio bytes\n", len - copyBytes);
                memcpy(&frame->data[0][ctx->bufferedAudio], &data[copyBytes], len - copyBytes);
            }
        }
    }

    return ret;
}

#endif
