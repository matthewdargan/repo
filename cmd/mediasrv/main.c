#include <dirent.h>
#include <fcntl.h>
#include <libavformat/avformat.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* clang-format off */
#include "libu/u.h"
#include "libu/arena.h"
#include "libu/string.h"
#include "libu/os.h"
#include "libu/u.c"
#include "libu/arena.c"
#include "libu/string.c"
#include "libu/os.c"
/* clang-format on */

typedef struct S64array S64array;
struct S64array {
	s64 *v;
	u64 cnt;
};

typedef struct Videocontext Videocontext;
struct Videocontext {
	String8 inputpath;
	String8 outputpath;
	AVFormatContext *inputctx;
	AVStream *videostream;
	S64array keyframes;
};

/*
static b32
extractkeyframes(Arena *a, Videocontext *ctx, String8 path)
{
    ctx->inputpath = pushstr8cpy(a, path);
    ctx->outputpath = pushstr8cpy(a, str8basename(str8prefixext(path)));
    if (!direxists(ctx->outputpath) && !osmkdir(ctx->outputpath)) {
        fprintf(stderr, "failed to create output directory: %s\n", ctx->outputpath.str);
        return 0;
    }
    int err = avformat_open_input(&ctx->inputctx, (char *)ctx->inputpath.str, NULL, NULL);
    if (err < 0) {
        fprintf(stderr, "failed to open input file %s: %s\n", ctx->inputpath.str, av_err2str(err));
        return 0;
    }
    err = avformat_find_stream_info(ctx->inputctx, NULL);
    if (err < 0) {
        fprintf(stderr, "failed to find stream info: %s\n", av_err2str(err));
        avformat_close_input(&ctx->inputctx);
        return 0;
    }
    int videostream = av_find_best_stream(ctx->inputctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (videostream < 0) {
        fprintf(stderr, "failed to find video stream: %s\n", av_err2str(videostream));
        avformat_close_input(&ctx->inputctx);
        return 0;
    }
    ctx->videostream = ctx->inputctx->streams[videostream];
    ctx->keyframes.v = pusharr(a, s64, 0x400);
    ctx->keyframes.cnt = 0;
    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "failed to allocate packet for key frame extraction\n");
        avformat_close_input(&ctx->inputctx);
        return 0;
    }
    while (av_read_frame(ctx->inputctx, pkt) >= 0) {
        if (pkt->stream_index == videostream && (pkt->flags & AV_PKT_FLAG_KEY)) {
            ctx->keyframes.v[ctx->keyframes.cnt] = pkt->pts;
            ctx->keyframes.cnt++;
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    err = av_seek_frame(ctx->inputctx, videostream, 0, AVSEEK_FLAG_BACKWARD);
    if (err < 0) {
        fprintf(stderr, "failed to seek to start of input file: %s\n", av_err2str(err));
        av_packet_free(&pkt);
        avformat_close_input(&ctx->inputctx);
        return 0;
    }
    return 1;
}

static b32
transmux_to_fmp4(String8 src, String8 dst)
{
    AVFormatContext *inputctx = NULL;
    AVFormatContext *outputctx = NULL;
    AVIOContext *output_io_ctx = NULL;
    AVPacket pkt;
    b32 success = 0;
    int err = avformat_open_input(&inputctx, (char *)src.str, NULL, NULL);
    if (err < 0) {
        fprintf(stderr, "failed to open input file %s: %s\n", src.str, av_err2str(err));
        goto cleanup;
    }
    err = avformat_find_stream_info(inputctx, NULL);
    if (err < 0) {
        fprintf(stderr, "failed to find stream info: %s\n", av_err2str(err));
        goto cleanup;
    }
    avformat_alloc_output_context2(&outputctx, NULL, NULL, (char *)dst.str);
    if (!outputctx) {
        fprintf(stderr, "failed to allocate output context\n");
        goto cleanup;
    }
    err = avio_open(&output_io_ctx, (char *)dst.str, AVIO_FLAG_WRITE);
    if (err < 0) {
        fprintf(stderr, "failed to open output file %s: %s\n", dst.str, av_err2str(err));
        goto cleanup;
    }
    outputctx->pb = output_io_ctx;
    for (u32 i = 0; i < inputctx->nb_streams; i++) {
        AVStream *instream = inputctx->streams[i];
        AVCodecParameters *incodecpar = instream->codecpar;
        if (incodecpar->codec_type != AVMEDIA_TYPE_VIDEO && incodecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
            incodecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            continue;
        }
        AVStream *outstream = avformat_new_stream(outputctx, NULL);
        if (!outstream) {
            fprintf(stderr, "failed to create output stream\n");
            goto cleanup;
        }
        err = avcodec_parameters_copy(outstream->codecpar, incodecpar);
        if (err < 0) {
            fprintf(stderr, "failed to copy codec parameters: %s\n", av_err2str(err));
            goto cleanup;
        }
        outstream->codecpar->codec_tag = 0;
    }
    opts = NULL;
    av_dict_set(&opts, "movflags", "frag_keyframe+empty_moov", 0);
    err = avformat_write_header(outputctx, &opts);
    av_dict_free(&opts);
    if (err < 0) {
        fprintf(stderr, "failed to write header: %s\n", av_err2str(err));
        goto cleanup;
    }
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;
    while (av_read_frame(inputctx, &pkt) >= 0) {
        AVStream *instream = inputctx->streams[pkt.stream_index];
        AVStream *outstream = NULL;
        for (u32 i = 0; i < outputctx->nb_streams; i++) {
            if (outputctx->streams[i]->codecpar->codec_type == instream->codecpar->codec_type &&
                outputctx->streams[i]->codecpar->codec_id == instream->codecpar->codec_id) {
                outstream = outputctx->streams[i];
                break;
            }
        }
        if (outstream) {
            pkt.pts = av_rescale_q_rnd(pkt.pts, instream->time_base, outstream->time_base,
                                       AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
            pkt.dts = av_rescale_q_rnd(pkt.dts, instream->time_base, outstream->time_base,
                                       AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
            pkt.duration = av_rescale_q(pkt.duration, instream->time_base, outstream->time_base);
            pkt.pos = -1;
            err = av_interleaved_write_frame(outputctx, &pkt);
            if (err < 0) {
                fprintf(stderr, "failed to write packet: %s\n", av_err2str(err));
                av_packet_unref(&pkt);
                goto cleanup;
            }
        }
        av_packet_unref(&pkt);
    }
    err = av_write_trailer(outputctx);
    if (err < 0) {
        fprintf(stderr, "failed to write trailer: %s\n", av_err2str(err));
        goto cleanup;
    }
    success = 1;
cleanup:
    if (inputctx) {
        avformat_close_input(&inputctx);
    }
    if (outputctx && outputctx->pb) {
        avio_closep(&outputctx->pb);
    }
    if (outputctx) {
        avformat_free_context(outputctx);
    }
    return success;
}
*/

static b32
transmux(Arena *a, String8 inputpath, String8 outputdir, String8 base_name, f64 segment_duration_sec)
{
	AVFormatContext *inputctx, *outputctx;
	AVPacket *pkt;
	b32 success;
	String8 segmenttemplate, mpdpath, segmenttime;
	int err;
	u64 i;
	AVStream *instream, *outstream;
	AVCodecParameters *incodecpar;
	AVDictionary *opts;

	inputctx = NULL;
	outputctx = NULL;
	pkt = av_packet_alloc();
	success = 0;
	segmenttemplate =
	    pushstr8f(a, "%.*s/%.*s_segment_%%04d.mp4", outputdir.len, outputdir.str, base_name.len, base_name.str);
	mpdpath = pushstr8f(a, "%.*s/%.*s.mpd", outputdir.len, outputdir.str, base_name.len, base_name.str);
	err = avformat_open_input(&inputctx, (char *)inputpath.str, NULL, NULL);
	if (err < 0) {
		fprintf(stderr, "failed to open input file %s: %s\n", inputpath.str, av_err2str(err));
		goto cleanup;
	}
	err = avformat_find_stream_info(inputctx, NULL);
	if (err < 0) {
		fprintf(stderr, "failed to find stream info: %s\n", av_err2str(err));
		goto cleanup;
	}
	avformat_alloc_output_context2(&outputctx, NULL, "segment", (char *)segmenttemplate.str);
	if (!outputctx) {
		fprintf(stderr, "failed to allocate output context for segment muxer\n");
		goto cleanup;
	}
	for (i = 0; i < inputctx->nb_streams; i++) {
		instream = inputctx->streams[i];
		incodecpar = instream->codecpar;
		if (incodecpar->codec_type != AVMEDIA_TYPE_VIDEO && incodecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
		    incodecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
			continue;
		}
		outstream = avformat_new_stream(outputctx, NULL);
		if (!outstream) {
			fprintf(stderr, "failed to create output stream\n");
			goto cleanup;
		}
		err = avcodec_parameters_copy(outstream->codecpar, incodecpar);
		if (err < 0) {
			fprintf(stderr, "failed to copy codec parameters: %s\n", av_err2str(err));
			goto cleanup;
		}
		/* Set codec tag to 0 (important for compatibility) */
		outstream->codecpar->codec_tag = 0;
	}
	/* Set options for the segment muxer and fMP4 segments */
	opts = NULL;
	/* format=mpd: Tell the segment muxer to output an MPD file */
	av_dict_set(&opts, "format", "mpd", 0);
	/* segment_format=mp4: Use MP4 format for the segments */
	av_dict_set(&opts, "segment_format", "mp4", 0);
	/*
	 * movflags for fMP4 segments
	 * frag_keyframe: Start a new fragment at each keyframe
	 * empty_moov: Write a fragmented MP4 that can be played while being written
	 * write_empty_moov: Write an empty moov box even if not fragmented (good practice for fMP4 init segment)
	 * default_base_moof: Use default-base-is-moof flag in traf boxes (common for DASH)
	 */
	av_dict_set(&opts, "movflags", "frag_keyframe+empty_moov+write_empty_moov+default_base_moof", 0);
	/* segment_time: Set segment duration in seconds */
	/* av_dict_set_double(&opts, "segment_time", segment_duration_sec, 0); */
	segmenttime = pushstr8f(a, "%f", segment_duration_sec);
	av_dict_set(&opts, "segment_time", (char *)segmenttime.str, 0);
	/* segment_list: Specify the name of the MPD file */
	av_dict_set(&opts, "segment_list", (char *)mpdpath.str, 0);
	/* segment_list_flags=live: Write MPD in a way suitable for dynamic updates (good for VOD too) */
	av_dict_set(&opts, "segment_list_flags", "live", 0);
	/* segment_individualheader: Write a header (moov box) in each segment (essential for fMP4 segments) */
	av_dict_set(&opts, "segment_individualheader", "1", 0);
	/* min_frag_duration: Minimum fragment duration in microseconds (optional, can help with small fragments) */
	/* av_dict_set_int(&opts, "min_frag_duration", 1000000, 0); */
	err = avformat_write_header(outputctx, &opts);
	av_dict_free(&opts);
	if (err < 0) {
		fprintf(stderr, "failed to write header (MPD/Init Segment): %s\n", av_err2str(err));
		goto cleanup;
	}
	pkt->data = NULL;
	pkt->size = 0;
	while (av_read_frame(inputctx, pkt) >= 0) {
		instream = inputctx->streams[pkt->stream_index];
		outstream = NULL;
		for (i = 0; i < outputctx->nb_streams; i++) {
			if (outputctx->streams[i]->codecpar->codec_type == instream->codecpar->codec_type &&
			    outputctx->streams[i]->codecpar->codec_id == instream->codecpar->codec_id) {
				outstream = outputctx->streams[i];
				break;
			}
		}
		if (outstream) {
			/*
			 * Rescale timestamps from input timebase to output timebase
			 * The segment muxer handles the actual writing to files based on segment_time
			 */
			pkt->pts = av_rescale_q_rnd(pkt->pts, instream->time_base, outstream->time_base,
			                            AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
			pkt->dts = av_rescale_q_rnd(pkt->dts, instream->time_base, outstream->time_base,
			                            AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
			pkt->duration = av_rescale_q(pkt->duration, instream->time_base, outstream->time_base);
			pkt->pos = -1; /* Let FFmpeg handle position */
			               /* Set the correct stream index for the output context */
			pkt->stream_index = outstream->index;
			err = av_interleaved_write_frame(outputctx, pkt);
			if (err < 0) {
				fprintf(stderr, "failed to write packet: %s\n", av_err2str(err));
				/* Continue processing other packets or break? Let's break on write error. */
				av_packet_unref(pkt);
				goto cleanup;
			}
		}
		av_packet_unref(pkt);
	}
	err = av_write_trailer(outputctx);
	if (err < 0) {
		fprintf(stderr, "failed to write trailer: %s\n", av_err2str(err));
		goto cleanup;
	}
	success = 1;
cleanup:
	if (inputctx)
		avformat_close_input(&inputctx);
	/* Note: avio_closep is NOT called here, the segment muxer manages the files */
	if (outputctx)
		avformat_free_context(outputctx);
	return success;
}

int
main(void)
{
	Arenaparams ap;
	Temp scratch;
	String8 inputpath, outputpath, outputdir;
	f64 segmentduration;

	sysinfo.nprocs = sysconf(_SC_NPROCESSORS_ONLN);
	sysinfo.pagesz = sysconf(_SC_PAGESIZE);
	sysinfo.lpagesz = 0x200000;
	ap.flags = arenaflags;
	ap.ressz = arenaressz;
	ap.cmtsz = arenacmtsz;
	arena = arenaalloc(ap);
	scratch = tempbegin(arena);
	av_log_set_level(AV_LOG_INFO);
	inputpath = str8lit("testdata/Big_Buck_Bunny_1080_10s_30MB_h265.mp4");
	outputpath = str8lit("output/Big_Buck_Bunny_1080_10s_30MB_h265_transmuxed.mp4");
	outputdir = str8lit("output");
	segmentduration = 2.0;
	if (!direxists(outputdir) && !osmkdir(outputdir)) {
		fprintf(stderr, "failed to create output directory: %s\n", outputdir.str);
		return 1;
	}
	printf("Transmuxing %s to %s...\n", inputpath.str, outputpath.str);
	if (transmux(scratch.a, inputpath, outputdir, str8lit("test"), segmentduration))
		printf("DASH segmentation successful.\n");
	else {
		fprintf(stderr, "DASH segmentation failed.\n");
		return 1;
	}
	tempend(scratch);
	return 0;
}
