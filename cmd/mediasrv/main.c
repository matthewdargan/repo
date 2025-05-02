#include <dirent.h>
#include <fcntl.h>
#include <libavformat/avformat.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// clang-format off
#include "libu/u.h"
#include "libu/arena.h"
#include "libu/string.h"
#include "libu/os.h"
#include "libu/u.c"
#include "libu/arena.c"
#include "libu/string.c"
#include "libu/os.c"
// clang-format on

typedef struct S64Array S64Array;
struct S64Array {
	s64 *v;
	u64 cnt;
};

typedef struct VideoContext VideoContext;
struct VideoContext {
	String8 input_path;
	String8 output_path;
	AVFormatContext *input_context;
	AVStream *video_stream;
	S64Array keyframes;
};

static b32
extract_keyframes(Arena *a, VideoContext *ctx, String8 path)
{
	ctx->input_path = push_str8_copy(a, path);
	ctx->output_path = push_str8_copy(a, str8_basename(str8_prefix_ext(path)));
	if (!os_dir_exists(ctx->output_path) && !os_mkdir(ctx->output_path)) {
		fprintf(stderr, "failed to create output directory: %s\n", ctx->output_path.str);
		return 0;
	}
	int err = avformat_open_input(&ctx->input_context, (char *)ctx->input_path.str, NULL, NULL);
	if (err < 0) {
		fprintf(stderr, "failed to open input file %s: %s\n", ctx->input_path.str, av_err2str(err));
		return 0;
	}
	err = avformat_find_stream_info(ctx->input_context, NULL);
	if (err < 0) {
		fprintf(stderr, "failed to find stream info: %s\n", av_err2str(err));
		avformat_close_input(&ctx->input_context);
		return 0;
	}
	int video_stream = av_find_best_stream(ctx->input_context, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (video_stream < 0) {
		fprintf(stderr, "failed to find video stream: %s\n", av_err2str(video_stream));
		avformat_close_input(&ctx->input_context);
		return 0;
	}
	ctx->video_stream = ctx->input_context->streams[video_stream];
	ctx->keyframes.v = push_array(a, s64, KB(1));
	ctx->keyframes.cnt = 0;
	AVPacket *pkt = av_packet_alloc();
	if (!pkt) {
		fprintf(stderr, "failed to allocate packet for key frame extraction\n");
		avformat_close_input(&ctx->input_context);
		return 0;
	}
	while (av_read_frame(ctx->input_context, pkt) >= 0) {
		if (pkt->stream_index == video_stream && (pkt->flags & AV_PKT_FLAG_KEY)) {
			ctx->keyframes.v[ctx->keyframes.cnt++] = pkt->pts;
		}
		av_packet_unref(pkt);
	}
	av_packet_free(&pkt);
	err = av_seek_frame(ctx->input_context, video_stream, 0, AVSEEK_FLAG_BACKWARD);
	if (err < 0) {
		fprintf(stderr, "failed to seek to start of input file: %s\n", av_err2str(err));
		av_packet_free(&pkt);
		avformat_close_input(&ctx->input_context);
		return 0;
	}
	return 1;
}

int
main(void)
{
	sys_info.nprocs = (u32)sysconf(_SC_NPROCESSORS_ONLN);
	sys_info.page_size = (u64)sysconf(_SC_PAGESIZE);
	sys_info.large_page_size = MB(2);
	arena = arena_alloc((ArenaParams){
	    .flags = arena_default_flags, .res_size = arena_default_res_size, .cmt_size = arena_default_cmt_size});
	Temp scratch = temp_begin(arena);
	String8 path = str8_lit("testdata/Big_Buck_Bunny_1080_10s_30MB_h265.mp4");
	VideoContext ctx = {0};
	if (!extract_keyframes(scratch.a, &ctx, path)) {
		fprintf(stderr, "failed to extract keyframes\n");
		temp_end(scratch);
		arena_release(arena);
		return 1;
	}
	if (ctx.keyframes.cnt == 0) {
		fprintf(stderr, "no key frames found\n");
	}
	for (u64 i = 0; i < ctx.keyframes.cnt; ++i) {
		printf("i=%ld,ctx.keyframes[i]=%ld\n", i, ctx.keyframes.v[i]);
	}
	printf("duration=%ld\n", ctx.video_stream->duration);
	avformat_close_input(&ctx.input_context);
	temp_end(scratch);
	arena_release(arena);
	return 0;
}
