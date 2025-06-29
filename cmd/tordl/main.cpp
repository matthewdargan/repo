#include <dirent.h>
#include <stdarg.h>
#include <sys/mman.h>

#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/session.hpp>

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

static String8array
parsetorrents(Arena *a, String8 data)
{
	String8array torrents;
	u64 nlines, i, eolpos, tabpos;
	String8 line, magnet;

	nlines = 0;
	for (i = 0; i < data.len; i++)
		nlines += (data.str[i] == '\n');
	torrents.v = pusharrnoz(a, String8, nlines);
	torrents.cnt = 0;
	line = str8zero();
	while (data.len > 0) {
		eolpos = str8index(data, 0, str8lit("\n"), 0);
		line = (eolpos == data.len) ? data : str8substr(data, rng1u64(0, eolpos));
		data = (eolpos == data.len) ? str8zero() : str8skip(data, eolpos + 1);
		tabpos = str8index(line, 0, str8lit("\t"), 0);
		if (tabpos < line.len) {
			magnet = str8substr(line, rng1u64(tabpos + 1, line.len));
			torrents.v[torrents.cnt] = pushstr8cpy(a, magnet);
			torrents.cnt++;
		}
	}
	return torrents;
}

static void
downloadtorrents(String8array torrents)
{
	libtorrent::settings_pack pack;
	u64 i, j;
	time_t lastupdate, now;
	libtorrent::add_torrent_params ps;
	libtorrent::error_code ec;
	b32 done;
	std::vector<libtorrent::torrent_handle> handles;
	libtorrent::torrent_handle h;
	libtorrent::torrent_status s;
	u8 *statusstr;

	if (torrents.cnt == 0)
		return;
	pack.set_int(libtorrent::settings_pack::alert_mask,
	             libtorrent::alert::status_notification | libtorrent::alert::error_notification);
	libtorrent::session session(pack);
	for (i = 0; i < torrents.cnt; i++) {
		try {
			libtorrent::parse_magnet_uri((char *)torrents.v[i].str, ps, ec);
			if (ec) {
				fprintf(stderr, "tordl: failed to parse magnet URI: %s\n", ec.message().c_str());
				continue;
			}
			ps.save_path = ".";
			session.add_torrent(ps);
		} catch (const std::exception &e) {
			fprintf(stderr, "tordl: failed to add torrent: %s\n", e.what());
		}
	}
	lastupdate = time(0);
	for (done = 0; !done;) {
		now = time(0);
		if (now - lastupdate >= 1) {
			lastupdate = now;
			handles = session.get_torrents();
			done = 1;
			for (j = 0; j < handles.size(); j++) {
				h = handles[j];
				if (!h.is_valid())
					continue;
				s = h.status();
				switch (s.state) {
					case libtorrent::torrent_status::seeding:
						statusstr = (u8 *)"seeding";
						break;
					case libtorrent::torrent_status::finished:
						statusstr = (u8 *)"finished";
						break;
					case libtorrent::torrent_status::downloading:
						statusstr = (u8 *)"downloading";
						break;
					default:
						statusstr = (u8 *)"other";
						break;
				}
				printf("tordl: name=%s, status=%s, downloaded=%ld, peers=%d\n", s.name.c_str(), statusstr, s.total_done,
				       s.num_peers);
				done &=
				    (s.state == libtorrent::torrent_status::seeding || s.state == libtorrent::torrent_status::finished);
			}
		}
		sleepms(100);
	}
	printf("tordl: downloads complete\n");
}

int
main(int argc, char *argv[])
{
	Arenaparams ap;
	String8list args;
	Cmd parsed;
	String8 path, data, chunk;
	u8 buf[4096];
	ssize_t n;
	String8array torrents;

	sysinfo.nprocs = (u32)sysconf(_SC_NPROCESSORS_ONLN);
	sysinfo.pagesz = (u64)sysconf(_SC_PAGESIZE);
	sysinfo.lpagesz = 0x200000;
	ap.flags = arenaflags;
	ap.ressz = arenaressz;
	ap.cmtsz = arenacmtsz;
	arena = arenaalloc(ap);
	args = osargs(arena, argc, argv);
	parsed = cmdparse(arena, args);
	path = str8zero();
	if (cmdhasarg(&parsed, str8lit("f")))
		path = cmdstr(&parsed, str8lit("f"));
	data = str8zero();
	if (path.len > 0) {
		data = readfile(arena, path);
		if (data.len == 0) {
			fprintf(stderr, "tordl: failed to read file: %s\n", path.str);
			arenarelease(arena);
			return 1;
		}
	} else {
		for (;;) {
			n = read(STDIN_FILENO, buf, sizeof buf);
			if (n <= 0)
				break;
			chunk = str8(buf, (u64)n);
			data = pushstr8cat(arena, data, chunk);
		}
	}
	if (data.len == 0) {
		fprintf(stderr, "tordl: no data read\n");
		arenarelease(arena);
		return 1;
	}
	torrents = parsetorrents(arena, data);
	if (torrents.cnt == 0) {
		fprintf(stderr, "tordl: no torrents found\n");
		arenarelease(arena);
		return 1;
	}
	downloadtorrents(torrents);
	arenarelease(arena);
	return 0;
}
