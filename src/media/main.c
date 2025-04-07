#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libavformat/avformat.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

typedef struct stream_variant stream_variant;
struct stream_variant {
    string8 name;
    string8 uri;
    u32 width;
    u32 height;
    u32 bitrate;
};

int entry_point(cmd_line *cmd_line) {
    temp scratch = temp_begin(g_arena);
    AVFormatContext *input_context = avformat_alloc_context();
    string8 input_path = str8_lit("testdata/h264_sample.mp4");
    if (avformat_open_input(&input_context, (char *)input_path.str, NULL, NULL) < 0) {
        fprintf(stderr, "failed to open input file\n");
        return 1;
    }
    if (avformat_find_stream_info(input_context, NULL) < 0) {
        fprintf(stderr, "failed to find stream info\n");
        return 1;
    }
    int video_stream_index = av_find_best_stream(input_context, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_stream_index < 0) {
        fprintf(stderr, "failed to find video stream\n");
        return 1;
    }
    f64array key_frames = {
        .v = push_array(scratch.a, f64, KB(1)),
        .count = 0,
    };
    AVPacket *packet = av_packet_alloc();
    AVStream *video_stream = input_context->streams[video_stream_index];
    while (av_read_frame(input_context, packet) >= 0) {
        if (packet->stream_index == video_stream_index) {
            if (packet->flags & AV_PKT_FLAG_KEY) {
                key_frames.v[key_frames.count++] = packet->pts * av_q2d(video_stream->time_base);
            }
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    AVFormatContext *output_context = NULL;
    if (avformat_alloc_output_context2(&output_context, NULL, "dash", "manifest.mpd") < 0) {
        fprintf(stderr, "failed to create output context\n");
        return 1;
    }
    AVStream *out_stream = avformat_new_stream(output_context, NULL);
    if (!out_stream) {
        fprintf(stderr, "failed to create output stream\n");
        return 1;
    }
    if (avcodec_parameters_copy(out_stream->codecpar, video_stream->codecpar) < 0) {
        fprintf(stderr, "failed to copy codec parameters\n");
        return 1;
    }
    AVDictionary *options = NULL;
    av_dict_set(&options, "adaptation_sets", "id=0,streams=v", 0);
    av_dict_set(&options, "seg_duration", "4", 0);  // 4 second segments
    av_dict_set(&options, "frag_type", "duration", 0);
    av_dict_set(&options, "frag_duration", "4", 0);  // 4 second fragments
    av_dict_set(&options, "window_size", "5", 0);
    av_dict_set(&options, "extra_window_size", "5", 0);
    av_dict_set(&options, "remove_at_exit", "0", 0);  // Keep files after program exits
    if (avio_open(&output_context->pb, "manifest.mpd", AVIO_FLAG_WRITE) < 0) {
        fprintf(stderr, "failed to open output file\n");
        return 1;
    }
    if (avformat_write_header(output_context, &options) < 0) {
        fprintf(stderr, "failed to write header\n");
        return 1;
    }
    if (av_write_trailer(output_context) < 0) {
        fprintf(stderr, "failed to write trailer\n");
        return 1;
    }
    av_dict_free(&options);
    avio_closep(&output_context->pb);
    avformat_free_context(output_context);
    avformat_close_input(&input_context);
    avformat_free_context(input_context);
    temp_end(scratch);
    return 0;
}
