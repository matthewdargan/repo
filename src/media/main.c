#include <dirent.h>
#include <fcntl.h>
#include <libavformat/avformat.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// clang-format off
#include "base/inc.h"
#include "os/inc.h"
#include "base/inc.c"
#include "os/inc.c"
// clang-format on

typedef struct f64array f64array;
struct f64array {
    f64 *v;
    u64 count;
};

typedef struct video_context video_context;
struct video_context {
    AVFormatContext *input_context;
    AVStream *video_stream;
    f64array key_frames;
    string8 input_path;
    string8 output_path;
};

typedef struct quality_info quality_info;
struct quality_info {
    u32 width;
    u32 height;
    u32 bit_rate;
};

read_only global quality_info qualities[] = {
    {1920, 1080, 4000000}, {1280, 720, 2000000}, {854, 480, 1000000}, {640, 360, 500000}};

b32 video_context_init(arena *a, video_context *ctx, string8 input_path) {
    ctx->input_path = input_path;
    ctx->output_path = str8_lit("output");
    ctx->input_context = avformat_alloc_context();
    if (avformat_open_input(&ctx->input_context, (char *)input_path.str, NULL, NULL) < 0) {
        fprintf(stderr, "failed to open input file\n");
        return 0;
    }
    if (avformat_find_stream_info(ctx->input_context, NULL) < 0) {
        fprintf(stderr, "failed to find stream info\n");
        return 0;
    }
    int video_stream_index = av_find_best_stream(ctx->input_context, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_stream_index < 0) {
        fprintf(stderr, "failed to find video stream\n");
        return 0;
    }
    ctx->video_stream = ctx->input_context->streams[video_stream_index];
    ctx->key_frames.v = push_array(a, f64, KB(1));
    ctx->key_frames.count = 0;
    AVPacket *packet = av_packet_alloc();
    while (av_read_frame(ctx->input_context, packet) >= 0) {
        if (packet->stream_index == video_stream_index) {
            if (packet->flags & AV_PKT_FLAG_KEY) {
                ctx->key_frames.v[ctx->key_frames.count++] = packet->pts * av_q2d(ctx->video_stream->time_base);
            }
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    return 1;
}

b32 generate_manifest(video_context *ctx) {
    AVFormatContext *output_context = NULL;
    if (avformat_alloc_output_context2(&output_context, NULL, "dash", "manifest.mpd") < 0) {
        fprintf(stderr, "failed to create output context\n");
        return 0;
    }
    for (u32 i = 0; i < ARRAY_COUNT(qualities); ++i) {
        AVStream *output_stream = avformat_new_stream(output_context, NULL);
        if (!output_stream) {
            fprintf(stderr, "failed to create output stream for quality %ux%u\n", qualities[i].width,
                    qualities[i].height);
            avformat_free_context(output_context);
            return 0;
        }
        if (avcodec_parameters_copy(output_stream->codecpar, ctx->video_stream->codecpar) < 0) {
            fprintf(stderr, "failed to copy codec parameters for quality %ux%u\n", qualities[i].width,
                    qualities[i].height);
            avformat_free_context(output_context);
            return 0;
        }
        output_stream->codecpar->width = qualities[i].width;
        output_stream->codecpar->height = qualities[i].height;
        output_stream->codecpar->bit_rate = qualities[i].bit_rate;
        output_stream->time_base = ctx->video_stream->time_base;
    }
    if (avio_open(&output_context->pb, "manifest.mpd", AVIO_FLAG_WRITE) < 0) {
        fprintf(stderr, "failed to open output file\n");
        avformat_free_context(output_context);
        return 0;
    }
    if (avformat_write_header(output_context, NULL) < 0) {
        fprintf(stderr, "failed to write header\n");
        avformat_free_context(output_context);
        return 0;
    }
    if (av_write_trailer(output_context) < 0) {
        fprintf(stderr, "failed to write trailer\n");
        avformat_free_context(output_context);
        return 0;
    }
    avio_closep(&output_context->pb);
    avformat_free_context(output_context);
    return 1;
}

// b32 transcode_segment(video_context *ctx, u32 segment_index, string8 quality) {
//     // TODO: only transcode if segment doesn't exist. use keyframes to determine exact segment boundaries. output to
//     output_path/segments/chunk-<quality>-<number>.m4s
// }

int entry_point(cmd_line *cmd_line) {
    temp scratch = temp_begin(g_arena);
    string8 input_path = str8_lit("testdata/h264_sample.mp4");
    video_context ctx = {0};
    if (!video_context_init(scratch.a, &ctx, input_path)) {
        return 1;
    }
    if (!generate_manifest(&ctx)) {
        return 1;
    }
    avformat_close_input(&ctx.input_context);
    avformat_free_context(ctx.input_context);
    temp_end(scratch);
    return 0;
}
