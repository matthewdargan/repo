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

typedef struct s64array s64array;
struct s64array {
	s64 *v;
	u64 count;
};

typedef struct video_context video_context;
struct video_context {
	string8 input_path;
	string8 output_path;
	AVFormatContext *input_context;
	AVStream *video_stream;
	s64array keyframes;
};

internal b32
extract_keyframes(arena *a, video_context *ctx, string8 path)
{
	ctx->input_path = push_str8_copy(a, path);
	ctx->output_path = push_str8_copy(a, str8_skip_last_slash(str8_chop_last_dot(path)));
	if (!os_folder_path_exists(ctx->output_path) && !os_make_directory(ctx->output_path)) {
		fprintf(stderr, "failed to create output directory: %s\n", ctx->output_path.str);
		return 0;
	}
	int ret = avformat_open_input(&ctx->input_context, (char *)ctx->input_path.str, nil, nil);
	if (ret < 0) {
		fprintf(stderr, "failed to open input file %s: %s\n", ctx->input_path.str, av_err2str(ret));
		return 0;
	}
	ret = avformat_find_stream_info(ctx->input_context, nil);
	if (ret < 0) {
		fprintf(stderr, "failed to find stream info: %s\n", av_err2str(ret));
		avformat_close_input(&ctx->input_context);
		return 0;
	}
	int video_stream = av_find_best_stream(ctx->input_context, AVMEDIA_TYPE_VIDEO, -1, -1, nil, 0);
	if (video_stream < 0) {
		fprintf(stderr, "failed to find video stream: %s\n", av_err2str(video_stream));
		avformat_close_input(&ctx->input_context);
		return 0;
	}
	ctx->video_stream = ctx->input_context->streams[video_stream];
	ctx->keyframes.v = push_array(a, s64, KB(1));
	ctx->keyframes.count = 0;
	AVPacket *pkt = av_packet_alloc();
	if (!pkt) {
		fprintf(stderr, "failed to allocate packet for key frame extraction\n");
		goto err;
	}
	while (av_read_frame(ctx->input_context, pkt) >= 0) {
		if (pkt->stream_index == video_stream && (pkt->flags & AV_PKT_FLAG_KEY)) {
			ctx->keyframes.v[ctx->keyframes.count++] = pkt->pts;
		}
		av_packet_unref(pkt);
	}
	av_packet_free(&pkt);
	ret = av_seek_frame(ctx->input_context, video_stream, 0, AVSEEK_FLAG_BACKWARD);
	if (ret < 0) {
		fprintf(stderr, "failed to seek to start of input file: %s\n", av_err2str(ret));
		goto err;
	}
	return 1;

err:
	av_packet_free(&pkt);
	avformat_close_input(&ctx->input_context);
	return 0;
}

int
entry_point(cmd_line *cmd_line)
{
	temp scratch = temp_begin(g_arena);
	string8 path = str8_lit("testdata/Big_Buck_Bunny_1080_10s_30MB_h265.mp4");
	video_context ctx = {0};
	if (!extract_keyframes(scratch.a, &ctx, path)) {
		fprintf(stderr, "failed to extract keyframes\n");
		temp_end(scratch);
		return 1;
	}
	if (ctx.keyframes.count == 0) {
		fprintf(stderr, "no key frames found\n");
	}
	for (u64 i = 0; i < ctx.keyframes.count; ++i) {
		printf("i=%ld,ctx.keyframes[i]=%ld\n", i, ctx.keyframes.v[i]);
	}
	printf("duration=%ld\n", ctx.video_stream->duration);
	avformat_close_input(&ctx.input_context);
	temp_end(scratch);
	return 0;
}
