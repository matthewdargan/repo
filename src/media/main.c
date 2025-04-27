#include <dirent.h>
#include <fcntl.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
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
	string8 input_path;
	string8 output_path;
	AVFormatContext *input_context;
	AVStream *video_stream;
	f64array key_frames;
	f64 duration;
	u32 segment_duration_msec;
};

typedef struct quality_info quality_info;
struct quality_info {
	string8 name;
	string8 init_segment_path;
	u32 width;
	u32 height;
	u32 bit_rate;
};

read_only global quality_info qualities[] = {
    {.name = str8_lit_comp("1080p"), .width = 1920, .height = 1080, .bit_rate = 4000000},
    {.name = str8_lit_comp("720p"), .width = 1280, .height = 720, .bit_rate = 2000000},
    {.name = str8_lit_comp("480p"), .width = 854, .height = 480, .bit_rate = 1000000},
    {.name = str8_lit_comp("360p"), .width = 640, .height = 360, .bit_rate = 500000}};

internal b32
vctx_init(arena *a, video_context *ctx, string8 input_path)
{
	ctx->input_path = push_str8_copy(a, input_path);
	ctx->output_path = str8_chop_last_dot(str8_skip_last_slash(input_path));
	ctx->segment_duration_msec = 4000;  // 4 second segments
	if (!os_folder_path_exists(ctx->output_path) && !os_make_directory(ctx->output_path)) {
		fprintf(stderr, "failed to create output directory: %s\n", ctx->output_path.str);
		return 0;
	}
	ctx->input_context = avformat_alloc_context();
	if (!ctx->input_context) {
		fprintf(stderr, "failed to allocate input context\n");
		return 0;
	}
	int ret = avformat_open_input(&ctx->input_context, (char *)ctx->input_path.str, NULL, NULL);
	if (ret < 0) {
		fprintf(stderr, "failed to open input file %s: %s\n", ctx->input_path.str, av_err2str(ret));
		return 0;
	}
	AVPacket *packet = NULL;
	ret = avformat_find_stream_info(ctx->input_context, NULL);
	if (ret < 0) {
		fprintf(stderr, "failed to find stream info: %s\n", av_err2str(ret));
		goto cleanup;
	}
	int video_stream_idx = av_find_best_stream(ctx->input_context, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (video_stream_idx < 0) {
		fprintf(stderr, "failed to find video stream: %s\n", av_err2str(video_stream_idx));
		ret = video_stream_idx;
		goto cleanup;
	}
	ctx->video_stream = ctx->input_context->streams[video_stream_idx];
	ctx->duration = ctx->input_context->duration / (f64)AV_TIME_BASE;
	ctx->key_frames.v = push_array(a, f64, KB(1));
	ctx->key_frames.count = 0;
	packet = av_packet_alloc();
	if (!packet) {
		fprintf(stderr, "failed to allocate packet for key frame extraction\n");
		ret = AVERROR(ENOMEM);
		goto cleanup;
	}
	while (av_read_frame(ctx->input_context, packet) >= 0) {
		if (packet->stream_index == video_stream_idx && (packet->flags & AV_PKT_FLAG_KEY)) {
			f64 key_frame_time = packet->pts * av_q2d(ctx->video_stream->time_base);
			ctx->key_frames.v[ctx->key_frames.count++] = key_frame_time;
		}
		av_packet_unref(packet);
	}
	av_packet_free(&packet);
	ret = av_seek_frame(ctx->input_context, video_stream_idx, 0, AVSEEK_FLAG_BACKWARD);
	if (ret < 0) {
		fprintf(stderr, "failed to seek to start of input file: %s\n", av_err2str(ret));
		goto cleanup;
	}
	return 1;

cleanup:
	av_packet_free(&packet);
	avformat_close_input(&ctx->input_context);
	return 0;
}

internal b32
init_segments(arena *a, video_context *ctx)
{
	for (u64 i = 0; i < ARRAY_COUNT(qualities); ++i) {
		string8 segment_path = push_str8f(a, (char *)"%s/init-stream%u.m4s", ctx->output_path.str, i);
		qualities[i].init_segment_path = push_str8_copy(a, segment_path);
		AVFormatContext *output_context = NULL;
		if (avformat_alloc_output_context2(&output_context, NULL, "mp4", (char *)segment_path.str) < 0) {
			fprintf(stderr, "failed to create output context\n");
			return 0;
		}
		AVStream *output_stream = avformat_new_stream(output_context, NULL);
		if (!output_stream) {
			fprintf(stderr, "failed to create output stream\n");
			avformat_free_context(output_context);
			return 0;
		}
		const AVCodec *encoder = avcodec_find_encoder(ctx->video_stream->codecpar->codec_id);
		if (!encoder) {
			fprintf(stderr, "failed to find encoder\n");
			avformat_free_context(output_context);
			return 0;
		}
		AVCodecContext *encoder_context = avcodec_alloc_context3(encoder);
		if (!encoder_context) {
			fprintf(stderr, "failed to allocate encoder context\n");
			avformat_free_context(output_context);
			return 0;
		}
		encoder_context->width = qualities[i].width;
		encoder_context->height = qualities[i].height;
		encoder_context->bit_rate = qualities[i].bit_rate;
		encoder_context->time_base = ctx->video_stream->time_base;
		encoder_context->framerate = ctx->video_stream->avg_frame_rate;
		encoder_context->gop_size = 30;  // Should align with the segment duration
		encoder_context->max_b_frames = 2;
		encoder_context->pix_fmt = AV_PIX_FMT_YUV420P;
		encoder_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		if (avcodec_open2(encoder_context, encoder, NULL) < 0) {
			fprintf(stderr, "failed to open encoder\n");
			avcodec_free_context(&encoder_context);
			avformat_free_context(output_context);
			return 0;
		}
		if (avcodec_parameters_from_context(output_stream->codecpar, encoder_context) < 0) {
			fprintf(stderr, "failed to copy encoder parameters\n");
			avcodec_free_context(&encoder_context);
			avformat_free_context(output_context);
			return 0;
		}
		// Set the flag to product initialization segment only
		output_context->flags |= AVFMT_FLAG_NOFILLIN | AVFMT_FLAG_FLUSH_PACKETS;
		if (avio_open(&output_context->pb, (char *)segment_path.str, AVIO_FLAG_WRITE) < 0) {
			fprintf(stderr, "failed to open output file\n");
			avcodec_free_context(&encoder_context);
			avformat_free_context(output_context);
			return 0;
		}
		if (avformat_write_header(output_context, NULL) < 0) {
			fprintf(stderr, "failed to write header\n");
			avcodec_free_context(&encoder_context);
			avio_closep(&output_context->pb);
			avformat_free_context(output_context);
			return 0;
		}
		if (av_write_trailer(output_context) < 0) {
			fprintf(stderr, "failed to write trailer\n");
			avcodec_free_context(&encoder_context);
			avio_closep(&output_context->pb);
			avformat_free_context(output_context);
			return 0;
		}
		avcodec_free_context(&encoder_context);
		avio_closep(&output_context->pb);
		avformat_free_context(output_context);
	}
	return 1;
}

internal b32
generate_manifest(arena *a, video_context *ctx)
{
	string8 manifest_path = push_str8_cat(a, ctx->output_path, str8_lit("/manifest.mpd"));
	string8 manifest_content = push_str8f(a, (char *)"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                                                "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\" "
                                                "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
                                                "xsi:schemaLocation=\"urn:mpeg:dash:schema:mpd:2011 DASH-MPD.xsd\" "
                                                "profiles=\"urn:mpeg:dash:profile:isoff-live:2011\" "
                                                "type=\"static\" "
                                                "mediaPresentationDuration=\"PT%fS\" "
                                                "minBufferTime=\"PT4S\">\n"
                                                "  <Period start=\"PT0S\" duration=\"PT%.3fS\">\n"
                                                "    <AdaptationSet contentType=\"video\" mimeType=\"video/mp4\" segmentAlignment=\"true\" startWithSAP=\"1\">\n",
                                                ctx->duration, ctx->duration);
	for (u64 i = 0; i < ARRAY_COUNT(qualities); ++i) {
		struct stat st = {0};
		if (stat((const char *)qualities[i].init_segment_path.str, &st) != 0) {
			fprintf(stderr, "failed to stat init segment file: %s\n", qualities[i].init_segment_path.str);
			return 0;
		}
		u64 init_range_end = (st.st_size > 0) ? st.st_size - 1 : 0;
		f64 avg_segment_duration_sec = ctx->duration / (ctx->key_frames.count > 1 ? ctx->key_frames.count - 1 : 1);
		u64 avg_duration_time = (u64)(avg_segment_duration_sec * 90000.0 + 0.5);
		string8 representation = push_str8f(a, (char *)"      <Representation id=\"%llu\" codecs=\"avc1.64001f\" "
                                                        "width=\"%u\" height=\"%u\" frameRate=\"%d/%d\" "
                                                        "bandwidth=\"%u\">\n"
                                                        "        <SegmentList timescale=\"90000\" duration=\"%llu\">\n"
                                                        "          <Initialization sourceURL=\"%s\" range=\"0-%llu\" />\n",
                                                        i,
                                                        qualities[i].width, qualities[i].height,
                                                        ctx->video_stream->avg_frame_rate.num,
                                                        ctx->video_stream->avg_frame_rate.den,
                                                        qualities[i].bit_rate,
                                                        avg_duration_time,
                                                        str8_skip_last_slash(qualities[i].init_segment_path).str,
                                                        init_range_end);
		for (u64 j = 0; j < ctx->key_frames.count - 1; ++j) {
			f64 start_time = ctx->key_frames.v[j];
			f64 end_time = ctx->key_frames.v[j + 1];
			f64 duration_sec = end_time - start_time;
			u64 duration = (u64)(duration_sec * 90000.0 + 0.5);
			string8 segment_path = push_str8f(a, (char *)"chunk-stream%llu-%llu.m4s", i, j + 1);
			string8 segment = push_str8f(a, (char *)"          <SegmentURL media=\"%s\" duration=\"%llu\"/>\n",
			                             segment_path.str, duration);
			representation = push_str8_cat(a, representation, segment);
		}
		representation =
		    push_str8_cat(a, representation, str8_lit("        </SegmentList>\n      </Representation>\n"));
		manifest_content = push_str8_cat(a, manifest_content, representation);
	}
	manifest_content = push_str8_cat(a, manifest_content, str8_lit("    </AdaptationSet>\n  </Period>\n</MPD>\n"));
	return os_append_data_to_file_path(manifest_path, manifest_content);
}

internal b32
transcode_segment(arena *a, video_context *ctx, string8 quality, u32 segment_idx)
{
	quality_info *qi = NULL;
	u64 quality_idx = 0;
	for (u64 i = 0; i < ARRAY_COUNT(qualities); ++i) {
		if (str8_match(quality, qualities[i].name, 0)) {
			qi = &qualities[i];
			quality_idx = i;
			break;
		}
	}
	if (!qi) {
		fprintf(stderr, "invalid quality: %s\n", quality.str);
		return 0;
	}
	if (segment_idx >= ctx->key_frames.count - 1) {
		fprintf(stderr, "segment index %u out of range\n", segment_idx);
		return 0;
	}
	f64 start_time = ctx->key_frames.v[segment_idx];
	f64 end_time = ctx->key_frames.v[segment_idx + 1];
	string8 segment_path =
	    push_str8f(a, (char *)"%s/chunk-stream%u-%u.m4s", ctx->output_path.str, quality_idx, segment_idx + 1);
	struct stat st = {0};
	if (stat((const char *)segment_path.str, &st) == 0) {
		fprintf(stderr, "segment file %s already exists\n", segment_path.str);
		return 1;
	}
	AVFormatContext *output_context = NULL;
	AVCodecContext *encoder_context = NULL;
	AVCodecContext *decoder_context = NULL;
	struct SwsContext *sws_context = NULL;
	AVPacket *packet = NULL;
	AVFrame *input_frame = NULL;
	AVFrame *output_frame = NULL;
	AVDictionary *opts = NULL;
	AVDictionary *muxer_opts = NULL;
	int ret = 0;
	packet = av_packet_alloc();
	input_frame = av_frame_alloc();
	if (!packet || !input_frame) {
		fprintf(stderr, "failed to allocate packet or frame\n");
		ret = AVERROR(ENOMEM);
		goto cleanup;
	}
	ret = avformat_alloc_output_context2(&output_context, NULL, "mp4", (char *)segment_path.str);
	if (ret < 0) {
		fprintf(stderr, "failed to create output context: %s\n", av_err2str(ret));
		goto cleanup;
	}
	AVStream *output_stream = avformat_new_stream(output_context, NULL);
	if (!output_stream) {
		fprintf(stderr, "failed to create output stream\n");
		ret = AVERROR(ENOMEM);
		goto cleanup;
	}
	const AVCodec *encoder = avcodec_find_encoder(ctx->video_stream->codecpar->codec_id);
	if (!encoder) {
		fprintf(stderr, "failed to find encoder\n");
		ret = AVERROR_ENCODER_NOT_FOUND;
		goto cleanup;
	}
	encoder_context = avcodec_alloc_context3(encoder);
	if (!encoder_context) {
		fprintf(stderr, "failed to allocate encoder context\n");
		ret = AVERROR(ENOMEM);
		goto cleanup;
	}
	encoder_context->width = qi->width;
	encoder_context->height = qi->height;
	encoder_context->bit_rate = qi->bit_rate;
	if (ctx->video_stream->avg_frame_rate.num > 0 && ctx->video_stream->avg_frame_rate.den > 0) {
		encoder_context->time_base = av_inv_q(ctx->video_stream->avg_frame_rate);
		encoder_context->framerate = ctx->video_stream->avg_frame_rate;
	} else if (ctx->video_stream->r_frame_rate.num > 0 && ctx->video_stream->r_frame_rate.den > 0) {
		encoder_context->time_base = av_inv_q(ctx->video_stream->r_frame_rate);
		encoder_context->framerate = ctx->video_stream->r_frame_rate;
	} else {
		fprintf(stderr, "warning: could not determine input frame rate, using 1/25\n");
		encoder_context->time_base = (AVRational){1, 25};
		encoder_context->framerate = (AVRational){25, 1};
	}
	encoder_context->gop_size = (int)(av_q2d(encoder_context->framerate) * 2 + 0.5);
	encoder_context->max_b_frames = 2;
	encoder_context->pix_fmt = AV_PIX_FMT_YUV420P;
	av_dict_set(&opts, "forced-idr", "1", 0);
	ret = avcodec_open2(encoder_context, encoder, &opts);
	av_dict_free(&opts);
	if (ret < 0) {
		fprintf(stderr, "failed to open encoder: %s\n", av_err2str(ret));
		goto cleanup;
	}
	ret = avcodec_parameters_from_context(output_stream->codecpar, encoder_context);
	if (ret < 0) {
		fprintf(stderr, "failed to copy encoder parameters: %s\n", av_err2str(ret));
		goto cleanup;
	}
	output_stream->time_base = encoder_context->time_base;
	if (!(output_context->oformat->flags & AVFMT_NOFILE)) {
		ret = avio_open(&output_context->pb, (char *)segment_path.str, AVIO_FLAG_WRITE);
		if (ret < 0) {
			fprintf(stderr, "failed to open output file %s: %s\n", segment_path.str, av_err2str(ret));
			goto cleanup;
		}
	}
	av_dict_set(&muxer_opts, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
	ret = avformat_write_header(output_context, &muxer_opts);
	av_dict_free(&muxer_opts);
	if (ret < 0) {
		fprintf(stderr, "failed to write header: %s\n", av_err2str(ret));
		goto cleanup;
	}
	const AVCodec *decoder = avcodec_find_decoder(ctx->video_stream->codecpar->codec_id);
	if (!decoder) {
		fprintf(stderr, "failed to find decoder for codec id %d\n", ctx->video_stream->codecpar->codec_id);
		ret = AVERROR_DECODER_NOT_FOUND;
		goto cleanup;
	}
	decoder_context = avcodec_alloc_context3(decoder);
	if (!decoder_context) {
		fprintf(stderr, "failed to allocate decoder context\n");
		ret = AVERROR(ENOMEM);
		goto cleanup;
	}
	ret = avcodec_parameters_to_context(decoder_context, ctx->video_stream->codecpar);
	if (ret < 0) {
		fprintf(stderr, "failed to copy decoder parameters: %s\n", av_err2str(ret));
		goto cleanup;
	}
	decoder_context->time_base = ctx->video_stream->time_base;
	ret = avcodec_open2(decoder_context, decoder, NULL);
	if (ret < 0) {
		fprintf(stderr, "failed to open decoder: %s\n", av_err2str(ret));
		goto cleanup;
	}
	sws_context = sws_getContext(decoder_context->width, decoder_context->height, decoder_context->pix_fmt,
	                             encoder_context->width, encoder_context->height, encoder_context->pix_fmt,
	                             SWS_BILINEAR, NULL, NULL, NULL);
	if (!sws_context) {
		fprintf(stderr, "failed to create sws context\n");
		ret = AVERROR(ENOMEM);
		goto cleanup;
	}
	int64_t seek_time =
	    av_rescale_q(start_time * AV_TIME_BASE, (AVRational){1, AV_TIME_BASE}, ctx->video_stream->time_base);
	ret = av_seek_frame(ctx->input_context, ctx->video_stream->index, seek_time, AVSEEK_FLAG_BACKWARD);
	if (ret < 0) {
		fprintf(stderr, "failed to seek in input file: %s\n", av_err2str(ret));
		goto cleanup;
	}
	avcodec_flush_buffers(decoder_context);
	// main transcoding loop
	b32 segment_end = 0;
	int64_t first_frame_time_base = AV_NOPTS_VALUE;
	int64_t next_expected_time_base = 0;
	while (!segment_end) {
		ret = av_read_frame(ctx->input_context, packet);
		if (ret < 0) {
			if (ret == AVERROR_EOF) {
				segment_end = 1;
				break;
			} else {
				fprintf(stderr, "failed to read frame: %s\n", av_err2str(ret));
				goto cleanup;
			}
		}
		if (packet->stream_index != ctx->video_stream->index) {
			av_packet_unref(packet);
			continue;
		}
		if (packet->pts != AV_NOPTS_VALUE) {
			f64 packet_time = packet->pts * av_q2d(ctx->video_stream->time_base);
			if (packet_time >= end_time) {
				segment_end = 1;
				av_packet_unref(packet);
				break;
			}
		} else {
			fprintf(stderr, "warning: packet has no pts\n");
		}
		ret = avcodec_send_packet(decoder_context, packet);
		if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
			fprintf(stderr, "failed to send packet to decoder: %s\n", av_err2str(ret));
			av_packet_unref(packet);
			goto cleanup;
		}
		// receive frames from decoder
		while (ret >= 0) {
			ret = avcodec_receive_frame(decoder_context, input_frame);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
				break;
			} else if (ret < 0) {
				fprintf(stderr, "failed to receive frame from decoder: %s\n", av_err2str(ret));
				av_packet_unref(packet);
				goto cleanup;
			}
			if (input_frame->pts == AV_NOPTS_VALUE) {
				fprintf(stderr, "warning: decoded frame has no pts\n");
				av_frame_unref(input_frame);
				continue;
			}
			f64 frame_time = input_frame->pts * av_q2d(decoder_context->time_base);
			if (frame_time < start_time) {
				av_frame_unref(input_frame);
				continue;
			}
			if (frame_time >= end_time) {
				segment_end = 1;
				av_frame_unref(input_frame);
				break;
			}
			if (first_frame_time_base == AV_NOPTS_VALUE) {
				first_frame_time_base = input_frame->pts;
				next_expected_time_base = 0;
			}
			output_frame = av_frame_alloc();
			if (!output_frame) {
				fprintf(stderr, "failed to allocate output frame\n");
				av_frame_unref(input_frame);
				ret = AVERROR(ENOMEM);
				goto cleanup;
			}
			output_frame->width = encoder_context->width;
			output_frame->height = encoder_context->height;
			output_frame->format = encoder_context->pix_fmt;
			int buf_ret = av_frame_get_buffer(output_frame, 32);
			if (buf_ret < 0) {
				fprintf(stderr, "failed to allocate output frame buffer: %s\n", av_err2str(buf_ret));
				ret = buf_ret;
				av_packet_unref(packet);
				goto cleanup;
			}
			sws_scale(sws_context, (const uint8_t *const *)input_frame->data, input_frame->linesize, 0,
			          decoder_context->height, output_frame->data, output_frame->linesize);
			int64_t pts_relative_time_base = input_frame->pts - first_frame_time_base;
			output_frame->pts = av_rescale_q_rnd(pts_relative_time_base, decoder_context->time_base,
			                                     encoder_context->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
			if (output_frame->pts < next_expected_time_base) {
				fprintf(stderr, "warning: output pts %ld is less than expected %ld\n", output_frame->pts,
				        next_expected_time_base);
				output_frame->pts = next_expected_time_base;
			}
			next_expected_time_base = output_frame->pts + 1;
			int send_ret = avcodec_send_frame(encoder_context, output_frame);
			av_frame_free(&output_frame);
			if (send_ret < 0 && send_ret != AVERROR(EAGAIN) && send_ret != AVERROR_EOF) {
				fprintf(stderr, "failed to send frame to encoder: %s\n", av_err2str(send_ret));
				ret = send_ret;
				av_packet_unref(packet);
				goto cleanup;
			}
			// receive encoded packets from encoder
			while (send_ret >= 0) {
				int recv_ret = avcodec_receive_packet(encoder_context, packet);
				if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF) {
					break;
				} else if (recv_ret < 0) {
					fprintf(stderr, "failed to receive packet from encoder: %s\n", av_err2str(recv_ret));
					ret = recv_ret;
					goto cleanup;
				}
				av_packet_rescale_ts(packet, encoder_context->time_base, output_stream->time_base);
				packet->duration = av_rescale_q(1, encoder_context->time_base, output_stream->time_base);
				packet->stream_index = 0;
				int write_ret = av_interleaved_write_frame(output_context, packet);
				if (write_ret < 0) {
					fprintf(stderr, "failed to write frame: %s\n", av_err2str(write_ret));
					ret = write_ret;
					av_packet_unref(packet);
					goto cleanup;
				}
			}
			av_frame_unref(input_frame);
		}
		if (ret != AVERROR(EAGAIN)) {
			av_packet_unref(packet);
		}
		if (segment_end) {
			break;
		}
	}
	// flush encoder
	ret = avcodec_send_frame(encoder_context, NULL);
	if (ret < 0 && ret != AVERROR_EOF) {
		fprintf(stderr, "failed to send flush frame to encoder: %s\n", av_err2str(ret));
		goto cleanup;
	}
	while (ret >= 0) {
		ret = avcodec_receive_packet(encoder_context, packet);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
			break;
		} else if (ret < 0) {
			fprintf(stderr, "failed to receive flush packet from encoder: %s\n", av_err2str(ret));
			goto cleanup;
		}
		av_packet_rescale_ts(packet, encoder_context->time_base, output_stream->time_base);
		packet->duration = av_rescale_q(1, encoder_context->time_base, output_stream->time_base);
		packet->stream_index = 0;
		int write_ret = av_interleaved_write_frame(output_context, packet);
		if (write_ret < 0) {
			fprintf(stderr, "failed to write flush frame: %s\n", av_err2str(ret));
			ret = write_ret;
			av_packet_unref(packet);
			goto cleanup;
		}
	}
	if (ret == AVERROR_EOF) {
		ret = 0;
	}
	ret = av_write_trailer(output_context);
	if (ret < 0) {
		fprintf(stderr, "failed to write trailer: %s\n", av_err2str(ret));
	}

cleanup:
	sws_freeContext(sws_context);
	avcodec_free_context(&decoder_context);
	avcodec_free_context(&encoder_context);
	if (output_context && !(output_context->oformat->flags & AVFMT_NOFILE) && output_context->pb) {
		avio_closep(&output_context->pb);
	}
	avformat_free_context(output_context);
	av_frame_free(&input_frame);
	av_frame_free(&output_frame);
	av_packet_free(&packet);
	return ret >= 0;
}

internal b32
transcode_all_segments(arena *a, video_context *ctx)
{
	if (!init_segments(a, ctx)) {
		return 0;
	}
	for (u64 i = 0; i < ARRAY_COUNT(qualities); ++i) {
		printf("transcoding %s\n", qualities[i].name.str);
		for (u64 j = 0; j < ctx->key_frames.count - 1; ++j) {
			printf("    segment %lu/%lu\n", j + 1, ctx->key_frames.count - 1);
			if (!transcode_segment(a, ctx, qualities[i].name, j)) {
				return 0;
			}
		}
		printf("    completed quality %s\n", qualities[i].name.str);
	}
	return 1;
}

internal b32
get_segment(arena *a, video_context *ctx, string8 quality, u32 segment_idx)
{
	quality_info *qi = NULL;
	u64 quality_idx = 0;
	for (u64 i = 0; i < ARRAY_COUNT(qualities); ++i) {
		if (str8_match(quality, qualities[i].name, 0)) {
			qi = &qualities[i];
			quality_idx = i;
			break;
		}
	}
	if (!qi) {
		fprintf(stderr, "invalid quality: %s\n", quality.str);
		return 0;
	}
	string8 segment_path =
	    push_str8f(a, (char *)"%s/chunk-stream%u-%u.m4s", ctx->output_path.str, quality_idx, segment_idx + 1);
	struct stat st = {0};
	if (stat((const char *)segment_path.str, &st) == 0) {
		fprintf(stderr, "segment file %s already exists\n", segment_path.str);
		return 1;
	}
	return transcode_segment(a, ctx, quality, segment_idx);
}

int
entry_point(cmd_line *cmd_line)
{
	temp scratch = temp_begin(g_arena);
	string8 input_path = str8_lit("testdata/h264_sample.mp4");
	video_context ctx = {0};
	int ret = 1;
	if (!vctx_init(scratch.a, &ctx, input_path)) {
		fprintf(stderr, "failed to initialize video context\n");
		temp_end(scratch);
		return 1;
	}
	if (ctx.key_frames.count == 0) {
		fprintf(stderr, "no key frames found\n");
		goto cleanup;
	}
	printf("ctx.key_frames.count: %lu\n", ctx.key_frames.count);
	printf("ctx.duration: %f\n", ctx.duration);
	printf("generating init segments\n");
	if (!init_segments(scratch.a, &ctx)) {
		fprintf(stderr, "failed to initialize segments\n");
		goto cleanup;
	}
	printf("generating manifest\n");
	if (!generate_manifest(scratch.a, &ctx)) {
		fprintf(stderr, "failed to generate manifest\n");
		goto cleanup;
	}
	printf("transcoding all segments\n");
	if (!transcode_all_segments(scratch.a, &ctx)) {
		fprintf(stderr, "failed to transcode all segments\n");
		goto cleanup;
	}
	printf("DASH packaging complete! content available at %s\n", ctx.output_path.str);
	ret = 0;

cleanup:
	avformat_close_input(&ctx.input_context);
	temp_end(scratch);
	return ret;
}
