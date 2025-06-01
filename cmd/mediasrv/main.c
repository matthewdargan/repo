#include <dirent.h>
#include <fcntl.h>
#include <libavformat/avformat.h>
#include <microhttpd.h>
#include <signal.h>
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

readonly static String8 subs = str8litc("/subtitles");
readonly static b32 run = 1;

static b32
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

static b32
validsub(AVStream *st)
{
	return st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE &&
	       (st->codecpar->codec_id == AV_CODEC_ID_ASS || st->codecpar->codec_id == AV_CODEC_ID_SSA);
}

static void
freesubs(AVFormatContext **subctxs, u64 n)
{
	u64 i;

	if (subctxs != NULL)
		for (i = 0; i < n; i++) {
			if (subctxs[i] != NULL) {
				av_write_trailer(subctxs[i]);
				avio_closep(&subctxs[i]->pb);
				avformat_free_context(subctxs[i]);
			}
		}
}

static int
mkmedia(Arena *a, String8 dir)
{
	AVFormatContext *ictx, *mpdctx, **subctxs;
	AVStream *istream, *mpdstream, **substreams;
	AVDictionaryEntry *bps, *le;
	AVDictionary *opts;
	String8 path, mpdpath, bpsstr, subdir, subpath, lang;
	int ret;
	U64array mpdstreams;
	u64 i, nmpds, mpdidx;
	AVPacket *pkt;
	s64 pts, dts, duration;

	ictx = NULL;
	mpdctx = NULL;
	subctxs = NULL;
	istream = NULL;
	mpdstream = NULL;
	bps = NULL;
	le = NULL;
	opts = NULL;
	pkt = av_packet_alloc();
	path = pushstr8cat(a, dir, str8lit("/video.mkv"));
	mpdpath = pushstr8cat(a, dir, str8lit("/manifest.mpd"));
	ret = avformat_open_input(&ictx, (char *)path.str, NULL, NULL);
	if (ret < 0) {
		fprintf(stderr, "mkmedia: can't open '%s'\n", path.str);
		return ret;
	}
	ret = avformat_find_stream_info(ictx, NULL);
	if (ret < 0) {
		fprintf(stderr, "mkmedia: can't find stream info\n");
		avformat_close_input(&ictx);
		return ret;
	}
	ret = avformat_alloc_output_context2(&mpdctx, NULL, "dash", (char *)mpdpath.str);
	if (ret < 0) {
		fprintf(stderr, "mkmedia: can't create MPD output context\n");
		avformat_close_input(&ictx);
		return ret;
	}
	subctxs = pusharr(a, AVFormatContext *, ictx->nb_streams);
	substreams = pusharr(a, AVStream *, ictx->nb_streams);
	mpdstreams.cnt = ictx->nb_streams;
	mpdstreams.v = pusharrnoz(a, u64, mpdstreams.cnt);
	nmpds = 0;
	for (i = 0; i < ictx->nb_streams; i++) {
		istream = ictx->streams[i];
		mpdstreams.v[i] = U64MAX;
		if (validstream(istream)) {
			mpdstream = avformat_new_stream(mpdctx, NULL);
			if (mpdstream == NULL) {
				freesubs(subctxs, ictx->nb_streams);
				avformat_free_context(mpdctx);
				avformat_close_input(&ictx);
				return ret;
			}
			ret = avcodec_parameters_copy(mpdstream->codecpar, istream->codecpar);
			if (ret < 0) {
				freesubs(subctxs, ictx->nb_streams);
				avformat_free_context(mpdctx);
				avformat_close_input(&ictx);
				return ret;
			}
			if (mpdstream->codecpar->bit_rate == 0) {
				bps = av_dict_get(istream->metadata, "BPS", NULL, 0);
				if (bps != NULL) {
					bpsstr = str8cstr(bps->value);
					mpdstream->codecpar->bit_rate = str8tou64(bpsstr, 10);
				}
			}
			if (mpdstream->codecpar->frame_size == 0) {
				switch (istream->codecpar->codec_id) {
					case AV_CODEC_ID_AAC:
						mpdstream->codecpar->frame_size = 1024;
						break;
					case AV_CODEC_ID_AC3:
					case AV_CODEC_ID_EAC3:
						mpdstream->codecpar->frame_size = 1536;
						break;
					case AV_CODEC_ID_FLAC:
						mpdstream->codecpar->frame_size = 4096;
						break;
					case AV_CODEC_ID_MP3:
						mpdstream->codecpar->frame_size = 1152;
						break;
					case AV_CODEC_ID_OPUS:
						mpdstream->codecpar->frame_size = 960;
						break;
					default:
						mpdstream->codecpar->frame_size = 1024;
						break;
				}
			}
			mpdstreams.v[i] = nmpds++;
		}
		if (validsub(istream)) {
			lang = str8lit("unknown");
			le = av_dict_get(istream->metadata, "language", NULL, 0);
			if (le != NULL)
				lang = str8cstr(le->value);
			subdir = pushstr8cat(a, dir, str8lit("/subtitles"));
			if (!direxists(subdir))
				osmkdir(subdir);
			subpath = pushstr8f(a, "%s/%s%lu.ass", subdir.str, lang.str, nowus());
			ret = avformat_alloc_output_context2(&subctxs[i], NULL, "ass", (char *)subpath.str);
			if (ret < 0) {
				fprintf(stderr, "mkmedia: can't create subtitle output context\n");
				continue;
			}
			substreams[i] = avformat_new_stream(subctxs[i], NULL);
			if (substreams[i] == NULL) {
				continue;
			}
			ret = avcodec_parameters_copy(substreams[i]->codecpar, istream->codecpar);
			if (ret < 0)
				continue;
			ret = avio_open(&subctxs[i]->pb, (char *)subpath.str, AVIO_FLAG_WRITE);
			if (ret < 0) {
				fprintf(stderr, "mkmedia: can't open subtitle file '%s'\n", subpath.str);
				continue;
			}
			ret = avformat_write_header(subctxs[i], NULL);
			if (ret < 0) {
				fprintf(stderr, "mkmedia: can't write subtitle header\n");
				continue;
			}
		}
	}
	av_dict_set(&opts, "hwaccel", "auto", 0);
	av_dict_set(&opts, "index_correction", "1", 0);
	av_dict_set(&opts, "streaming", "1", 0);
	ret = avio_open(&mpdctx->pb, (char *)mpdpath.str, AVIO_FLAG_WRITE);
	if (ret < 0) {
		fprintf(stderr, "mkmedia: can't open MPD output file '%s'\n", mpdpath.str);
		av_dict_free(&opts);
		freesubs(subctxs, ictx->nb_streams);
		avformat_free_context(mpdctx);
		avformat_close_input(&ictx);
		return ret;
	}
	ret = avformat_write_header(mpdctx, &opts);
	av_dict_free(&opts);
	if (ret < 0) {
		fprintf(stderr, "mkmedia: can't write MPD header\n");
		freesubs(subctxs, ictx->nb_streams);
		avio_closep(&mpdctx->pb);
		avformat_free_context(mpdctx);
		avformat_close_input(&ictx);
		return ret;
	}
	while (av_read_frame(ictx, pkt) >= 0) {
		i = pkt->stream_index;
		istream = ictx->streams[i];
		mpdidx = mpdstreams.v[i];
		if (mpdidx != U64MAX) {
			mpdstream = mpdctx->streams[mpdidx];
			pts = pkt->pts;
			dts = pkt->dts;
			duration = pkt->duration;
			if (pts == AV_NOPTS_VALUE)
				pts = 0;
			if (dts == AV_NOPTS_VALUE)
				dts = 0;
			pkt->pts = av_rescale_q_rnd(pts, istream->time_base, mpdstream->time_base, AV_ROUND_NEAR_INF);
			pkt->dts = av_rescale_q_rnd(dts, istream->time_base, mpdstream->time_base, AV_ROUND_NEAR_INF);
			pkt->duration = av_rescale_q(duration, istream->time_base, mpdstream->time_base);
			pkt->pos = -1;
			pkt->stream_index = mpdidx;
			ret = av_interleaved_write_frame(mpdctx, pkt);
			if (ret < 0) {
				fprintf(stderr, "mkmedia: can't write MPD frame\n");
				break;
			}
		}
		if (subctxs[i] != NULL && substreams[i] != NULL) {
			pkt->stream_index = 0;
			av_packet_rescale_ts(pkt, istream->time_base, substreams[i]->time_base);
			ret = av_interleaved_write_frame(subctxs[i], pkt);
			if (ret < 0) {
				fprintf(stderr, "mkmedia: can't write subtitle frame\n");
				break;
			}
		}
		av_packet_unref(pkt);
	}
	ret = av_write_trailer(mpdctx);
	if (ret < 0)
		fprintf(stderr, "mkmedia: can't write MPD trailer\n");
	av_packet_free(&pkt);
	freesubs(subctxs, ictx->nb_streams);
	avio_closep(&mpdctx->pb);
	avformat_free_context(mpdctx);
	avformat_close_input(&ictx);
	return ret;
}

static int
sendsub(Temp scratch, struct MHD_Connection *conn, String8 path)
{
	String8 subtitles;
	DIR *dir;
	struct dirent *entry;
	struct MHD_Response *resp;
	int ret;

	subtitles = str8zero();
	dir = opendir((char *)path.str);
	if (dir == NULL) {
		tempend(scratch);
		return MHD_NO;
	}
	entry = readdir(dir);
	while (entry != NULL) {
		if (entry->d_type == DT_REG) {
			subtitles = pushstr8cat(scratch.a, subtitles, str8cstr((char *)entry->d_name));
			subtitles = pushstr8cat(scratch.a, subtitles, str8lit("\n"));
		}
		entry = readdir(dir);
	}
	closedir(dir);
	resp = MHD_create_response_from_buffer(subtitles.len, (void *)subtitles.str, MHD_RESPMEM_MUST_COPY);
	MHD_add_response_header(resp, "Content-Type", "text/plain");
	ret = MHD_queue_response(conn, MHD_HTTP_OK, resp);
	MHD_destroy_response(resp);
	return ret;
}

static String8
mimetype(String8 path)
{
	String8 ext;

	ext = str8ext(path);
	if (str8cmp(ext, str8lit("mpd"), 0))
		return str8lit("application/dash+xml");
	else if (str8cmp(ext, str8lit("m4s"), 0))
		return str8lit("video/mp4");
	else if (str8cmp(ext, str8lit("ass"), 0))
		return str8lit("text/plain");
	else
		return str8lit("application/octet-stream");
}

static int
sendfile(struct MHD_Connection *conn, String8 path, String8 mime)
{
	int fd;
	Fprops props;
	struct MHD_Response *resp;
	int ret;

	fd = openfd(path, O_RDONLY);
	if (fd == 0)
		return MHD_NO;
	props = osfstat(fd);
	if (props.size == 0) {
		closefd(fd);
		return MHD_NO;
	}
	resp = MHD_create_response_from_fd(props.size, fd);
	if (resp == NULL) {
		close(fd);
		return MHD_NO;
	}
	MHD_add_response_header(resp, "Content-Type", (char *)mime.str);
	ret = MHD_queue_response(conn, MHD_HTTP_OK, resp);
	MHD_destroy_response(resp);
	return ret;
}

static enum MHD_Result
reqhandler(void *, struct MHD_Connection *conn, const char *url, const char *method, const char *, const char *,
           size_t *, void **)
{
	Temp scratch;
	String8 urlstr, mime, mediadir, resptxt;
	struct MHD_Response *resp;
	int ret;

	if (strcmp(method, "GET") != 0)
		return MHD_NO;
	urlstr = str8cstr((char *)url);
	if (str8index(urlstr, 0, str8lit(".."), 0) < urlstr.len)
		return MHD_NO;
	if (urlstr.len > 0 && urlstr.str[0] == '/')
		urlstr = str8skip(urlstr, 1);
	scratch = tempbegin(arena);
	if (str8cmp(str8suffix(urlstr, subs.len), subs, 0)) {
		if (direxists(urlstr))
			return sendsub(scratch, conn, urlstr);
		mediadir = str8dirname(urlstr);
		if (mkmedia(scratch.a, mediadir) >= 0 && direxists(urlstr))
			return sendsub(scratch, conn, urlstr);
		resptxt = str8lit("mediasrv: can't generate subtitles");
		resp = MHD_create_response_from_buffer(resptxt.len, (void *)resptxt.str, MHD_RESPMEM_MUST_COPY);
		ret = MHD_queue_response(conn, MHD_HTTP_NOT_FOUND, resp);
		MHD_destroy_response(resp);
		tempend(scratch);
		return ret;
	}
	if (fileexists(urlstr)) {
		mime = mimetype(urlstr);
		ret = sendfile(conn, urlstr, mime);
		tempend(scratch);
		return ret;
	}
	if (str8index(urlstr, 0, str8lit("/subtitles/"), 0)) {
		resptxt = str8lit("mediasrv: can't find subtitle file");
		resp = MHD_create_response_from_buffer(resptxt.len, (void *)resptxt.str, MHD_RESPMEM_MUST_COPY);
		ret = MHD_queue_response(conn, MHD_HTTP_NOT_FOUND, resp);
		MHD_destroy_response(resp);
		tempend(scratch);
		return ret;
	}
	mediadir = str8dirname(urlstr);
	if (mkmedia(scratch.a, mediadir) >= 0 && fileexists(urlstr)) {
		mime = mimetype(urlstr);
		ret = sendfile(conn, urlstr, mime);
		tempend(scratch);
		return ret;
	}
	resptxt = str8lit("mediasrv: can't generate media");
	resp = MHD_create_response_from_buffer(resptxt.len, (void *)resptxt.str, MHD_RESPMEM_MUST_COPY);
	ret = MHD_queue_response(conn, MHD_HTTP_NOT_FOUND, resp);
	MHD_destroy_response(resp);
	tempend(scratch);
	return ret;
}

static void
sighandler(int)
{
	run = 0;
}

int
main(int argc, char *argv[])
{
	Arenaparams ap;
	String8list args;
	Cmd parsed;
	String8 portstr;
	u64 port;
	struct MHD_Daemon *daemon;

	sysinfo.nprocs = sysconf(_SC_NPROCESSORS_ONLN);
	sysinfo.pagesz = sysconf(_SC_PAGESIZE);
	sysinfo.lpagesz = 0x200000;
	ap.flags = arenaflags;
	ap.ressz = arenaressz;
	ap.cmtsz = arenacmtsz;
	arena = arenaalloc(ap);
	args = osargs(arena, argc, argv);
	parsed = cmdparse(arena, args);
	portstr = str8zero();
	port = 8080;
	if (cmdhasarg(&parsed, str8lit("p"))) {
		portstr = cmdstr(&parsed, str8lit("p"));
		port = str8tou64(portstr, 10);
	}
	daemon = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION, port, NULL, NULL, &reqhandler, arena, MHD_OPTION_END);
	if (daemon == NULL) {
		fprintf(stderr, "mediasrv: can't start HTTP server\n");
		arenarelease(arena);
		return 1;
	}
	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);
	while (run)
		sleepms(1000);
	MHD_stop_daemon(daemon);
	return 0;
}
