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

int
mkmpd(Arena *a, String8 path, String8 dir)
{
	int ret;
	u64 i;
	AVFormatContext *ictx, *octx;
	AVStream *istream, *ostream;
	String8 mpdpath;
	AVDictionary *opts;
	AVPacket *pkt;
	s64 pts, dts, duration;

	ictx = NULL;
	octx = NULL;
	istream = NULL;
	ostream = NULL;
	opts = NULL;
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
	for (i = 0; i < ictx->nb_streams; i++) {
		istream = ictx->streams[i];
		ostream = avformat_new_stream(octx, NULL);
		if (ostream == NULL) {
			ret = AVERROR(ENOMEM);
			goto end;
		}
		ret = avcodec_parameters_copy(ostream->codecpar, istream->codecpar);
		if (ret < 0)
			goto end;
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
	pkt = av_packet_alloc();
	while (av_read_frame(ictx, pkt) >= 0) {
		istream = ictx->streams[pkt->stream_index];
		ostream = octx->streams[pkt->stream_index];
		pts = pkt->pts;
		dts = pkt->dts;
		duration = pkt->duration;
		pkt->pts = av_rescale_q_rnd(pts, istream->time_base, ostream->time_base, AV_ROUND_NEAR_INF);
		pkt->dts = av_rescale_q_rnd(dts, istream->time_base, ostream->time_base, AV_ROUND_NEAR_INF);
		pkt->duration = av_rescale_q(duration, istream->time_base, ostream->time_base);
		pkt->pos = -1;
		ret = av_interleaved_write_frame(octx, pkt);
		if (ret < 0) {
			fprintf(stderr, "Failed to write frame\n");
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
	if (opts)
		av_dict_free(&opts);
	if (octx && octx->pb)
		avio_closep(&octx->pb);
	if (octx)
		avformat_free_context(octx);
	if (ictx)
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
