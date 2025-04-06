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

int entry_point(cmd_line *cmd_line) {
    temp scratch = temp_begin(g_arena);
    AVFormatContext *input_context = avformat_alloc_context();
    string8 input_path = str8_lit("testdata/h264_sample.mp4");
    if (avformat_open_input(&input_context, (char *)input_path.str, NULL, NULL) < 0) {
        printf("failed to open input file\n");
        return 1;
    }
    if (avformat_find_stream_info(input_context, NULL) < 0) {
        printf("failed to find stream info\n");
        return 1;
    }
    int video_stream_index = av_find_best_stream(input_context, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_stream_index < 0) {
        printf("failed to find video stream\n");
        return 1;
    }
    AVPacket *packet = av_packet_alloc();
    AVStream *video_stream = input_context->streams[video_stream_index];
    while (av_read_frame(input_context, packet) >= 0) {
        if (packet->stream_index == video_stream_index) {
            if (packet->flags & AV_PKT_FLAG_KEY) {
                printf("%f\n", packet->pts * av_q2d(video_stream->time_base));
            }
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    avformat_close_input(&input_context);
    avformat_free_context(input_context);
    temp_end(scratch);
    return 0;
}
