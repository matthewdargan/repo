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

// static b32
// extract_keyframes(Arena *a, VideoContext *ctx, String8 path)
//{
//	ctx->input_path = push_str8_copy(a, path);
//	ctx->output_path = push_str8_copy(a, str8_basename(str8_prefix_ext(path)));
//	if (!os_dir_exists(ctx->output_path) && !os_mkdir(ctx->output_path)) {
//		fprintf(stderr, "failed to create output directory: %s\n", ctx->output_path.str);
//		return 0;
//	}
//	int err = avformat_open_input(&ctx->input_context, (char *)ctx->input_path.str, NULL, NULL);
//	if (err < 0) {
//		fprintf(stderr, "failed to open input file %s: %s\n", ctx->input_path.str, av_err2str(err));
//		return 0;
//	}
//	err = avformat_find_stream_info(ctx->input_context, NULL);
//	if (err < 0) {
//		fprintf(stderr, "failed to find stream info: %s\n", av_err2str(err));
//		avformat_close_input(&ctx->input_context);
//		return 0;
//	}
//	int video_stream = av_find_best_stream(ctx->input_context, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
//	if (video_stream < 0) {
//		fprintf(stderr, "failed to find video stream: %s\n", av_err2str(video_stream));
//		avformat_close_input(&ctx->input_context);
//		return 0;
//	}
//	ctx->video_stream = ctx->input_context->streams[video_stream];
//	ctx->keyframes.v = push_array(a, s64, KB(1));
//	ctx->keyframes.cnt = 0;
//	AVPacket *pkt = av_packet_alloc();
//	if (!pkt) {
//		fprintf(stderr, "failed to allocate packet for key frame extraction\n");
//		avformat_close_input(&ctx->input_context);
//		return 0;
//	}
//	while (av_read_frame(ctx->input_context, pkt) >= 0) {
//		if (pkt->stream_index == video_stream && (pkt->flags & AV_PKT_FLAG_KEY)) {
//			ctx->keyframes.v[ctx->keyframes.cnt++] = pkt->pts;
//		}
//		av_packet_unref(pkt);
//	}
//	av_packet_free(&pkt);
//	err = av_seek_frame(ctx->input_context, video_stream, 0, AVSEEK_FLAG_BACKWARD);
//	if (err < 0) {
//		fprintf(stderr, "failed to seek to start of input file: %s\n", av_err2str(err));
//		av_packet_free(&pkt);
//		avformat_close_input(&ctx->input_context);
//		return 0;
//	}
//	return 1;
// }

static b32
transmux_to_fmp4(String8 src, String8 dst)
{
	AVFormatContext *input_ctx = NULL;
	AVFormatContext *output_ctx = NULL;
	AVIOContext *output_io_ctx = NULL;
	AVPacket pkt;
	b32 success = 0;
	int err = avformat_open_input(&input_ctx, (char *)src.str, NULL, NULL);
	if (err < 0) {
		fprintf(stderr, "failed to open input file %s: %s\n", src.str, av_err2str(err));
		goto cleanup;
	}
	err = avformat_find_stream_info(input_ctx, NULL);
	if (err < 0) {
		fprintf(stderr, "failed to find stream info: %s\n", av_err2str(err));
		goto cleanup;
	}
	avformat_alloc_output_context2(&output_ctx, NULL, NULL, (char *)dst.str);
	if (!output_ctx) {
		fprintf(stderr, "failed to allocate output context\n");
		goto cleanup;
	}
	err = avio_open(&output_io_ctx, (char *)dst.str, AVIO_FLAG_WRITE);
	if (err < 0) {
		fprintf(stderr, "failed to open output file %s: %s\n", dst.str, av_err2str(err));
		goto cleanup;
	}
	output_ctx->pb = output_io_ctx;
	for (u32 i = 0; i < input_ctx->nb_streams; ++i) {
		AVStream *in_stream = input_ctx->streams[i];
		AVCodecParameters *in_codecpar = in_stream->codecpar;
		if (in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO && in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
		    in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
			continue;
		}
		AVStream *out_stream = avformat_new_stream(output_ctx, NULL);
		if (!out_stream) {
			fprintf(stderr, "failed to create output stream\n");
			goto cleanup;
		}
		err = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
		if (err < 0) {
			fprintf(stderr, "failed to copy codec parameters: %s\n", av_err2str(err));
			goto cleanup;
		}
		out_stream->codecpar->codec_tag = 0;
	}
	AVDictionary *opts = NULL;
	// frag_keyframe: Start a new fragment at each keyframe
	// empty_moov: Write a fragmented MP4 that can be played while being written
	//            (useful for live or dynamic scenarios, though this is transmuxing a file)
	//            For strict on-demand, empty_moov might not be strictly necessary but is common.
	//            Let's use it as it's common for DASH fMP4.
	av_dict_set(&opts, "movflags", "frag_keyframe+empty_moov", 0);
	err = avformat_write_header(output_ctx, &opts);
	av_dict_free(&opts);
	if (err < 0) {
		fprintf(stderr, "failed to write header: %s\n", av_err2str(err));
		goto cleanup;
	}
	av_init_packet(&pkt);
	pkt.data = NULL;
	pkt.size = 0;
	while (av_read_frame(input_ctx, &pkt) >= 0) {
		AVStream *in_stream = input_ctx->streams[pkt.stream_index];
		AVStream *out_stream = NULL;
		for (u32 i = 0; i < output_ctx->nb_streams; ++i) {
			if (output_ctx->streams[i]->codecpar->codec_type == in_stream->codecpar->codec_type &&
			    output_ctx->streams[i]->codecpar->codec_id == in_stream->codecpar->codec_id) {
				out_stream = output_ctx->streams[i];
				break;
			}
		}
		if (out_stream) {
			pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base,
			                           AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
			pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base,
			                           AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
			pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
			pkt.pos = -1;
			err = av_interleaved_write_frame(output_ctx, &pkt);
			if (err < 0) {
				fprintf(stderr, "failed to write packet: %s\n", av_err2str(err));
				av_packet_unref(&pkt);
				goto cleanup;
			}
		}
		av_packet_unref(&pkt);
	}
	err = av_write_trailer(output_ctx);
	if (err < 0) {
		fprintf(stderr, "failed to write trailer: %s\n", av_err2str(err));
		goto cleanup;
	}
	success = 1;
cleanup:
	if (input_ctx) {
		avformat_close_input(&input_ctx);
	}
	if (output_ctx && output_ctx->pb) {
		avio_closep(&output_ctx->pb);
	}
	if (output_ctx) {
		avformat_free_context(output_ctx);
	}
	return success;
}

int
main(void)
{
	sys_info.nprocs = (u32)sysconf(_SC_NPROCESSORS_ONLN);
	sys_info.page_size = (u64)sysconf(_SC_PAGESIZE);
	sys_info.large_page_size = MB(2);
	arena = arena_alloc((ArenaParams){
	    .flags = arena_default_flags, .res_size = arena_default_res_size, .cmt_size = arena_default_cmt_size});
	av_log_set_level(AV_LOG_INFO);
	String8 input_path = str8_lit("testdata/Big_Buck_Bunny_1080_10s_30MB_h265.mp4");
	String8 output_path = str8_lit("output/Big_Buck_Bunny_1080_10s_30MB_h265_transmuxed.mp4");
	String8 output_dir = str8_lit("output");
	if (!os_dir_exists(output_dir) && !os_mkdir(output_dir)) {
		fprintf(stderr, "failed to create output directory: %s\n", output_dir.str);
		return 1;
	}
	printf("Transmuxing %s to %s...\n", input_path.str, output_path.str);
	if (transmux_to_fmp4(input_path, output_path)) {
		printf("Transmuxing successful.\n");
	} else {
		fprintf(stderr, "Transmuxing failed.\n");
		return 1;
	}
	return 0;
}
