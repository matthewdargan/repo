#include <dirent.h>
#include <fcntl.h>
#include <libavformat/avformat.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* clang-format off */
#include "libu/u.h"
#include "libu/arena.h"
#include "libu/string.h"
#include "libu/cmd.h"
#include "libu/os.h"
#include "libu/socket.h"
#include "libu/u.c"
#include "libu/arena.c"
#include "libu/string.c"
#include "libu/cmd.c"
#include "libu/os.c"
#include "libu/socket.c"
/* clang-format on */

static String8 imtpt, omtpt;

static b32
validstream(AVStream *st)
{
	switch (st->codecpar->codec_type) {
		case AVMEDIA_TYPE_VIDEO:
			switch (st->codecpar->codec_id) {
				case AV_CODEC_ID_H264:
				case AV_CODEC_ID_VP9:
				case AV_CODEC_ID_HEVC:
				case AV_CODEC_ID_AV1:
					return 1;
				default:
					return 0;
			}
			break;
		case AVMEDIA_TYPE_AUDIO:
			switch (st->codecpar->codec_id) {
				case AV_CODEC_ID_MP3:
				case AV_CODEC_ID_AAC:
				case AV_CODEC_ID_AC3:
				case AV_CODEC_ID_EAC3:
				case AV_CODEC_ID_OPUS:
					return 1;
				default:
					return 0;
			}
			break;
		case AVMEDIA_TYPE_SUBTITLE:
			return st->codecpar->codec_id == AV_CODEC_ID_WEBVTT;
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

static String8
mkmedia(Arena *a, String8 ipath, String8 opath)
{
	AVFormatContext *ictx, *mpdctx, **subctxs;
	AVStream *istream, *mpdstream, **substreams;
	AVDictionaryEntry *bps, *le;
	AVDictionary *opts;
	String8list generated;
	Stringjoin join;
	String8 mpdpath, bpsstr, subpath, lang;
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
	memset(&generated, 0, sizeof generated);
	pkt = av_packet_alloc();
	mpdpath = pushstr8f(a, "%.*s/manifest%lu.mpd", opath.len, opath.str, nowus());
	ret = avformat_open_input(&ictx, (char *)ipath.str, NULL, NULL);
	if (ret < 0) {
		fprintf(stderr, "mkmedia: can't open '%s'\n", ipath.str);
		return str8zero();
	}
	ret = avformat_find_stream_info(ictx, NULL);
	if (ret < 0) {
		fprintf(stderr, "mkmedia: can't find stream info\n");
		avformat_close_input(&ictx);
		return str8zero();
	}
	ret = avformat_alloc_output_context2(&mpdctx, NULL, "dash", (char *)mpdpath.str);
	if (ret < 0) {
		fprintf(stderr, "mkmedia: can't create MPD output context\n");
		avformat_close_input(&ictx);
		return str8zero();
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
				return str8zero();
			}
			ret = avcodec_parameters_copy(mpdstream->codecpar, istream->codecpar);
			if (ret < 0) {
				freesubs(subctxs, ictx->nb_streams);
				avformat_free_context(mpdctx);
				avformat_close_input(&ictx);
				return str8zero();
			}
			if (mpdstream->codecpar->bit_rate == 0) {
				bps = av_dict_get(istream->metadata, "BPS", NULL, 0);
				if (bps != NULL) {
					bpsstr = str8cstr(bps->value);
					mpdstream->codecpar->bit_rate = str8tou64(bpsstr, 10);
				}
			}
			if (mpdstream->codecpar->frame_size == 0)
				switch (istream->codecpar->codec_id) {
					case AV_CODEC_ID_MP3:
						mpdstream->codecpar->frame_size = 1152;
						break;
					case AV_CODEC_ID_AC3:
					case AV_CODEC_ID_EAC3:
						mpdstream->codecpar->frame_size = 1536;
						break;
					case AV_CODEC_ID_FLAC:
						mpdstream->codecpar->frame_size = 4096;
						break;
					case AV_CODEC_ID_OPUS:
						mpdstream->codecpar->frame_size = 960;
						break;
					default:
						mpdstream->codecpar->frame_size = 1024;
						break;
				}
			mpdstreams.v[i] = nmpds++;
		}
		if (validsub(istream)) {
			lang = str8lit("unknown");
			le = av_dict_get(istream->metadata, "language", NULL, 0);
			if (le != NULL)
				lang = str8cstr(le->value);
			subpath = pushstr8f(a, "%.*s/%.*s%lu.ass", opath.len, opath.str, lang.len, lang.str, nowus());
			ret = avformat_alloc_output_context2(&subctxs[i], NULL, "ass", (char *)subpath.str);
			if (ret < 0) {
				fprintf(stderr, "mkmedia: can't create subtitle output context\n");
				continue;
			}
			substreams[i] = avformat_new_stream(subctxs[i], NULL);
			if (substreams[i] == NULL)
				continue;
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
			str8listpush(a, &generated, subpath);
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
		return str8zero();
	}
	ret = avformat_write_header(mpdctx, &opts);
	av_dict_free(&opts);
	if (ret < 0) {
		fprintf(stderr, "mkmedia: can't write MPD header\n");
		freesubs(subctxs, ictx->nb_streams);
		avio_closep(&mpdctx->pb);
		avformat_free_context(mpdctx);
		avformat_close_input(&ictx);
		return str8zero();
	}
	str8listpush(a, &generated, mpdpath);
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
	join.pre = str8lit("");
	join.sep = str8lit("\n");
	join.post = str8lit("");
	av_packet_free(&pkt);
	freesubs(subctxs, ictx->nb_streams);
	avio_closep(&mpdctx->pb);
	avformat_free_context(mpdctx);
	avformat_close_input(&ictx);
	return str8listjoin(a, &generated, &join);
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
	else if (str8cmp(ext, str8lit("html"), 0))
		return str8lit("text/html");
	else if (str8cmp(ext, str8lit("js"), 0))
		return str8lit("application/javascript");
	else if (str8cmp(ext, str8lit("css"), 0))
		return str8lit("text/css");
	else
		return str8lit("application/octet-stream");
}

static void
sendresp(Arena *a, u64 fd, String8 status, String8 mime, String8 body)
{
	String8 resp;

	resp = pushstr8f(a, "HTTP/1.1 %.*s\r\nContent-Type: %.*s\r\nContent-Length: %lu\r\n\r\n%.*s", status.len,
	                 status.str, mime.len, mime.str, body.len, body.len, body.str);
	socketwrite(fd, resp);
}

static void
sendfile(Arena *a, u64 clientfd, String8 path, String8 mime)
{
	u64 fd;
	Fprops props;
	String8 hdr, data, resp;

	fd = openfd(path, O_RDONLY);
	if (fd == 0) {
		sendresp(a, clientfd, str8lit("404 Not Found"), str8lit("text/plain"), str8lit("not found"));
		return;
	}
	props = osfstat(fd);
	hdr = pushstr8f(a, "HTTP/1.1 200 OK\r\nContent-Type: %.*s\r\nContent-Length: %lu\r\n\r\n", mime.len, mime.str,
	                props.size);
	data = readfilerng(a, fd, rng1u64(0, props.size));
	resp = pushstr8cat(a, hdr, data);
	socketwrite(clientfd, resp);
	closefd(fd);
}

static String8
listdir(Arena *a, String8 path)
{
	String8 p;
	DIR *d;
	String8list files;
	Stringjoin join;
	struct dirent *de;
	String8 dirname;

	p = pushstr8cpy(a, path);
	d = opendir((char *)p.str);
	if (d == NULL)
		return str8zero();
	memset(&files, 0, sizeof files);
	join.pre = str8lit("");
	join.sep = str8lit("\n");
	join.post = str8lit("");
	for (;;) {
		de = readdir(d);
		if (de == NULL)
			break;
		dirname = pushstr8cpy(a, str8cstr(de->d_name));
		str8listpush(a, &files, dirname);
	}
	closedir(d);
	return str8listjoin(a, &files, &join);
}

static void *
handleconn(void *arg)
{
	u64 clientfd, lineend, methodend, urlend;
	Arenaparams ap;
	Arena *a;
	String8 req, line, url, path, mime, resptxt, ext, mtpt, name, opath, indexpath;

	clientfd = (u64)arg;
	ap.flags = arenaflags;
	ap.ressz = arenaressz;
	ap.cmtsz = arenacmtsz;
	a = arenaalloc(ap);
	req = socketreadhttp(a, clientfd);
	if (req.len == 0) {
		arenarelease(a);
		closefd(clientfd);
		return NULL;
	}
	lineend = str8index(req, 0, str8lit("\r\n"), 0);
	if (lineend == req.len) {
		arenarelease(a);
		closefd(clientfd);
		return NULL;
	}
	line = str8prefix(req, lineend);
	methodend = str8index(line, 0, str8lit(" "), 0);
	if (methodend == line.len) {
		arenarelease(a);
		closefd(clientfd);
		return NULL;
	}
	urlend = str8index(line, methodend + 1, str8lit(" "), 0);
	if (urlend == line.len)
		url = str8skip(line, methodend + 1);
	else
		url = str8substr(line, rng1u64(methodend + 1, urlend));
	if (url.len == 0 || str8cmp(url, str8lit("/"), 0)) {
		path = str8lit("web/index.html");
		if (fileexists(path))
			sendfile(a, clientfd, path, str8lit("text/html"));
		else
			sendresp(a, clientfd, str8lit("404 Not Found"), str8lit("text/plain"), str8lit("not found"));
		arenarelease(a);
		closefd(clientfd);
		return NULL;
	}
	if (str8index(url, 0, str8lit(".."), 0) < url.len) {
		sendresp(a, clientfd, str8lit("400 Bad Request"), str8lit("text/plain"), str8lit("bad request"));
		arenarelease(a);
		closefd(clientfd);
		return NULL;
	}
	if (str8index(url, 0, str8lit("/css/"), 0) == 0 || str8index(url, 0, str8lit("/js/"), 0) == 0) {
		if (imtpt.len == 0)
			path = pushstr8cat(a, str8lit("web"), url);
		else
			path = pushstr8f(a, "%.*s/web%.*s", imtpt.len, imtpt.str, url.len, url.str);
		if (fileexists(path)) {
			mime = mimetype(path);
			sendfile(a, clientfd, path, mime);
		} else {
			resptxt = str8lit("mediasrv: can't find static file");
			sendresp(a, clientfd, str8lit("404 Not Found"), str8lit("text/plain"), resptxt);
		}
		arenarelease(a);
		closefd(clientfd);
		return NULL;
	}
	ext = str8ext(url);
	if (str8cmp(ext, str8lit("mkv"), 0) || str8cmp(ext, str8lit("mp4"), 0)) {
		if (imtpt.len == 0)
			path = pushstr8cpy(a, str8skip(url, 1));
		else
			path = pushstr8cat(a, imtpt, url);
		if (!fileexists(path)) {
			resptxt = str8lit("mediasrv: can't generate media");
			sendresp(a, clientfd, str8lit("404 Not Found"), str8lit("text/plain"), resptxt);
			arenarelease(a);
			closefd(clientfd);
			return NULL;
		}
		if (omtpt.len == 0)
			mtpt = str8dirname(path);
		else
			mtpt = pushstr8cpy(a, omtpt);
		name = str8prefixext(str8basename(url));
		opath = pushstr8f(a, "%.*s/%.*s", mtpt.len, mtpt.str, name.len, name.str);
		indexpath = pushstr8cat(a, opath, str8lit("-index"));
		if (fileexists(indexpath))
			resptxt = readfile(a, indexpath);
		else {
			if (!direxists(opath))
				osmkdir(opath);
			resptxt = mkmedia(a, path, opath);
			if (resptxt.len > 0)
				appendfile(indexpath, resptxt);
		}
		if (resptxt.len > 0)
			sendresp(a, clientfd, str8lit("200 OK"), str8lit("text/plain"), resptxt);
		else {
			resptxt = str8lit("mediasrv: can't generate media");
			sendresp(a, clientfd, str8lit("404 Not Found"), str8lit("text/plain"), resptxt);
		}
		arenarelease(a);
		closefd(clientfd);
		return NULL;
	}
	path = str8skip(url, 1);
	if (direxists(path)) {
		resptxt = listdir(a, path);
		if (resptxt.len > 0)
			sendresp(a, clientfd, str8lit("200 OK"), str8lit("text/plain"), resptxt);
		else {
			resptxt = str8lit("mediasrv: can't list directory");
			sendresp(a, clientfd, str8lit("404 Not Found"), str8lit("text/plain"), resptxt);
		}
	} else if (fileexists(path)) {
		mime = mimetype(path);
		sendfile(a, clientfd, path, mime);
	} else {
		resptxt = str8lit("mediasrv: can't find media");
		sendresp(a, clientfd, str8lit("404 Not Found"), str8lit("text/plain"), resptxt);
	}
	arenarelease(a);
	closefd(clientfd);
	return NULL;
}

int
main(int argc, char *argv[])
{
	Arenaparams ap;
	String8list args;
	Cmd parsed;
	String8 portstr;
	u64 srvfd, clientfd;
	pthread_t thread;

	sysinfo.nprocs = sysconf(_SC_NPROCESSORS_ONLN);
	sysinfo.pagesz = sysconf(_SC_PAGESIZE);
	sysinfo.lpagesz = 0x200000;
	ap.flags = arenaflags;
	ap.ressz = arenaressz;
	ap.cmtsz = arenacmtsz;
	arena = arenaalloc(ap);
	args = osargs(arena, argc, argv);
	parsed = cmdparse(arena, args);
	imtpt = str8zero();
	omtpt = str8zero();
	if (cmdhasarg(&parsed, str8lit("i")))
		imtpt = cmdstr(&parsed, str8lit("i"));
	if (cmdhasarg(&parsed, str8lit("o")))
		omtpt = cmdstr(&parsed, str8lit("o"));
	if (cmdhasarg(&parsed, str8lit("p")))
		portstr = cmdstr(&parsed, str8lit("p"));
	else
		portstr = str8lit("8080");
	if (!direxists(omtpt))
		osmkdir(omtpt);
	srvfd = socketlisten(portstr);
	if (srvfd == 0) {
		fprintf(stderr, "mediasrv: can't start HTTP server\n");
		arenarelease(arena);
		return 1;
	}
	for (;;) {
		clientfd = socketaccept(srvfd);
		if (clientfd == 0)
			continue;
		if (pthread_create(&thread, NULL, handleconn, (void *)clientfd) != 0)
			closefd(clientfd);
		pthread_detach(thread);
	}
	closefd(srvfd);
	arenarelease(arena);
	return 0;
}
