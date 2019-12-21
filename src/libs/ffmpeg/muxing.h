#if(C_STREAM)

#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

// a wrapper around a single output AVStream
typedef struct OutputStream {
    AVStream *st;
    AVCodecContext *enc;

    /* pts of the next frame that will be generated */
    int64_t next_pts;
    int samples_count;

    AVFrame *frame;
    AVFrame *tmp_frame;

    struct SwsContext *sws_ctx;
    struct SwrContext *swr_ctx;

    AVPacket *pkt;
} OutputStream;

typedef struct StreamContext {
    AVFormatContext *oc;
    AVCodec *audio_codec;
    AVCodec *video_codec;
    OutputStream video_st;
    OutputStream audio_st;
	int	width;
    int height;
	int fps;
    int bufferedAudio;
    Bitu frames;
} StreamContext;

#ifdef __cplusplus
extern "C"
{
#endif

int streaming_video_line(StreamContext* ctx, int y, Bit8u *data);
int streaming_video(StreamContext* ctx);
int streaming_audio(StreamContext* ctx, Bit32u len, Bit16s *data);

int streaming_init(const char *streamname, StreamContext* ctx);
int streaming_cleanup(StreamContext* ctx);

#ifdef __cplusplus
} // extern "C"
#endif

#endif