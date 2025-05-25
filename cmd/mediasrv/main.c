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
#include "libu/cmd.h"
#include "libu/os.h"
#include "libu/u.c"
#include "libu/arena.c"
#include "libu/string.c"
#include "libu/cmd.c"
#include "libu/os.c"
/* clang-format on */

typedef struct U64array U64array;
struct U64array {
	u64 *v;
	u64 cnt;
};

static int
validstream(AVStream *st)
{
	switch (st->codecpar->codec_type) {
		case AVMEDIA_TYPE_VIDEO:
			switch (st->codecpar->codec_id) {
				case AV_CODEC_ID_H264:
				case AV_CODEC_ID_HEVC:
				case AV_CODEC_ID_VP9:
				case AV_CODEC_ID_AV1:
					return 1;
				default:
					return 0;
			}
			break;
		case AVMEDIA_TYPE_AUDIO:
			switch (st->codecpar->codec_id) {
				case AV_CODEC_ID_AAC:
				case AV_CODEC_ID_MP3:
				case AV_CODEC_ID_AC3:
				case AV_CODEC_ID_EAC3:
				case AV_CODEC_ID_OPUS:
					return 1;
				default:
					return 0;
			}
			break;
		case AVMEDIA_TYPE_SUBTITLE:
			switch (st->codecpar->codec_id) {
				case AV_CODEC_ID_WEBVTT:
					return 1;
				default:
					return 0;
			}
			break;
		default:
			return 0;
	}
	return 0;
}

int
mkmpd(Arena *a, String8 path, String8 dir)
{
	AVFormatContext *ictx, *octx;
	AVStream *istream, *ostream;
	AVDictionaryEntry *bps;
	AVDictionary *opts;
	String8 mpdpath, bpsstr;
	int ret;
	U64array streams;
	u64 i, nostreams, oidx;
	AVPacket *pkt;
	s64 pts, dts, duration;

	ictx = NULL;
	octx = NULL;
	istream = NULL;
	ostream = NULL;
	bps = NULL;
	opts = NULL;
	pkt = av_packet_alloc();
	mpdpath = pushstr8cat(a, dir, str8lit("/manifest.mpd"));
	ret = avformat_open_input(&ictx, (char *)path.str, NULL, NULL);
	if (ret < 0) {
		fprintf(stderr, "mkmpd: can't open %s\n", path.str);
		goto end;
	}
	ret = avformat_find_stream_info(ictx, NULL);
	if (ret < 0) {
		fprintf(stderr, "mkmpd: can't find stream info\n");
		goto end;
	}
	avformat_alloc_output_context2(&octx, NULL, "dash", (char *)mpdpath.str);
	if (octx == NULL) {
		fprintf(stderr, "mkmpd: can't create output context\n");
		ret = -1;
		goto end;
	}
	streams.cnt = ictx->nb_streams;
	streams.v = pusharrnoz(a, u64, streams.cnt);
	nostreams = 0;
	for (i = 0; i < ictx->nb_streams; i++) {
		streams.v[i] = U64MAX;
		istream = ictx->streams[i];
		if (!validstream(istream))
			continue;
		ostream = avformat_new_stream(octx, NULL);
		if (ostream == NULL) {
			ret = AVERROR(ENOMEM);
			goto end;
		}
		ret = avcodec_parameters_copy(ostream->codecpar, istream->codecpar);
		if (ret < 0)
			goto end;
		if (ostream->codecpar->bit_rate == 0) {
			bps = av_dict_get(istream->metadata, "BPS", NULL, 0);
			if (bps != NULL) {
				bpsstr = str8cstr(bps->value);
				ostream->codecpar->bit_rate = str8tou64(bpsstr, 10);
			}
		}
		if (ostream->codecpar->frame_size == 0) {
			switch (istream->codecpar->codec_id) {
				case AV_CODEC_ID_AAC:
					ostream->codecpar->frame_size = 1024;
					break;
				case AV_CODEC_ID_AC3:
				case AV_CODEC_ID_EAC3:
					ostream->codecpar->frame_size = 1536;
					break;
				case AV_CODEC_ID_FLAC:
					ostream->codecpar->frame_size = 4096;
					break;
				case AV_CODEC_ID_MP3:
					ostream->codecpar->frame_size = 1152;
					break;
				case AV_CODEC_ID_OPUS:
					ostream->codecpar->frame_size = 960;
					break;
				default:
					ostream->codecpar->frame_size = 1024;
					break;
			}
		}
		streams.v[i] = nostreams;
		nostreams++;
	}
	av_dict_set(&opts, "hwaccel", "auto", 0);
	av_dict_set(&opts, "index_correction", "1", 0);
	av_dict_set(&opts, "streaming", "1", 0);
	ret = avio_open(&octx->pb, (char *)mpdpath.str, AVIO_FLAG_WRITE);
	if (ret < 0) {
		fprintf(stderr, "mkmpd: can't open output file %s\n", mpdpath.str);
		goto end;
	}
	ret = avformat_write_header(octx, &opts);
	if (ret < 0) {
		fprintf(stderr, "mkmpd: can't write header\n");
		goto end;
	}
	while (av_read_frame(ictx, pkt) >= 0) {
		oidx = streams.v[pkt->stream_index];
		if (oidx == U64MAX) {
			av_packet_unref(pkt);
			continue;
		}
		istream = ictx->streams[pkt->stream_index];
		ostream = octx->streams[oidx];
		pts = pkt->pts;
		dts = pkt->dts;
		duration = pkt->duration;
		if (pts == AV_NOPTS_VALUE)
			pts = 0;
		if (dts == AV_NOPTS_VALUE)
			dts = 0;
		pkt->pts = av_rescale_q_rnd(pts, istream->time_base, ostream->time_base, AV_ROUND_NEAR_INF);
		pkt->dts = av_rescale_q_rnd(dts, istream->time_base, ostream->time_base, AV_ROUND_NEAR_INF);
		pkt->duration = av_rescale_q(duration, istream->time_base, ostream->time_base);
		pkt->pos = -1;
		pkt->stream_index = oidx;
		ret = av_interleaved_write_frame(octx, pkt);
		if (ret < 0) {
			fprintf(stderr, "mkmpd: can't write frame\n");
			break;
		}
		av_packet_unref(pkt);
	}
	ret = av_write_trailer(octx);
	if (ret < 0) {
		fprintf(stderr, "mkmpd: can't write trailer\n");
		goto end;
	}
	ret = 0;
end:
	if (pkt != NULL)
		av_packet_free(&pkt);
	if (opts != NULL)
		av_dict_free(&opts);
	if (octx != NULL) {
		avio_closep(&octx->pb);
		avformat_free_context(octx);
	}
	if (ictx != NULL)
		avformat_close_input(&ictx);
	return ret;
}

int
main(int argc, char *argv[])
{
	Arenaparams ap;
	String8list args;
	Cmd parsed;
	Temp scratch;
	String8 path, dir;

	sysinfo.nprocs = sysconf(_SC_NPROCESSORS_ONLN);
	sysinfo.pagesz = sysconf(_SC_PAGESIZE);
	sysinfo.lpagesz = 0x200000;
	ap.flags = arenaflags;
	ap.ressz = arenaressz;
	ap.cmtsz = arenacmtsz;
	arena = arenaalloc(ap);
	args = osargs(arena, argc, argv);
	parsed = cmdparse(arena, args);
	scratch = tempbegin(arena);
	path = str8zero();
	if (cmdhasarg(&parsed, str8lit("p")))
		path = cmdstr(&parsed, str8lit("p"));
	if (path.len == 0) {
		fprintf(stderr, "usage: mediasrv -p path\n");
		return 1;
	}
	dir = str8lit("output");
	if (!direxists(dir) && !osmkdir(dir)) {
		fprintf(stderr, "mediasrv: cannot create directory '%s'\n", dir.str);
		return 1;
	}
	if (mkmpd(scratch.a, path, dir) < 0)
		return 1;
	tempend(scratch);
	return 0;
}
