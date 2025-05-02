#include <dirent.h>
#include <stdarg.h>
#include <sys/mman.h>

#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/session.hpp>

// clang-format off
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
// clang-format on

static String8Array
parse_torrents(Arena *a, String8 data)
{
	String8Array torrents = {0};
	u64 nlines = 0;
	for (u64 i = 0; i < data.len; ++i) {
		nlines += (data.str[i] == '\n');
	}
	torrents.v = push_array_no_zero(a, String8, nlines);
	for (String8 line = str8_zero(); data.len > 0;) {
		u64 eol_pos = str8_index(data, 0, str8_lit("\n"), 0);
		line = (eol_pos == data.len) ? data : str8_substr(data, rng1u64(0, eol_pos));
		data = (eol_pos == data.len) ? str8_zero() : str8_skip(data, eol_pos + 1);
		u64 tab_pos = str8_index(line, 0, str8_lit("\t"), 0);
		if (tab_pos < line.len) {
			String8 magnet = str8_substr(line, rng1u64(tab_pos + 1, line.len));
			torrents.v[torrents.cnt++] = push_str8_copy(a, magnet);
		}
	}
	return torrents;
}

static void
download_torrents(String8Array torrents)
{
	if (torrents.cnt == 0) {
		return;
	}
	libtorrent::settings_pack pack;
	pack.set_int(libtorrent::settings_pack::alert_mask,
	             libtorrent::alert::status_notification | libtorrent::alert::error_notification);
	libtorrent::session session(pack);
	for (u64 i = 0; i < torrents.cnt; ++i) {
		try {
			libtorrent::add_torrent_params ps;
			libtorrent::error_code ec;
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
	time_t last_update = time(0);
	for (b32 all_done = 0; !all_done;) {
		time_t now = time(0);
		if (now - last_update >= 1) {
			last_update = now;
			std::vector<libtorrent::torrent_handle> handles = session.get_torrents();
			all_done = 1;
			for (libtorrent::torrent_handle h : handles) {
				if (!h.is_valid()) {
					continue;
				}
				libtorrent::torrent_status s = h.status();
				u8 *status_str;
				switch (s.state) {
					case libtorrent::torrent_status::seeding:
						status_str = (u8 *)"seeding";
						break;
					case libtorrent::torrent_status::finished:
						status_str = (u8 *)"finished";
						break;
					case libtorrent::torrent_status::downloading:
						status_str = (u8 *)"downloading";
						break;
					default:
						status_str = (u8 *)"other";
						break;
				}
				printf("tordl: name=%s, status=%s, downloaded=%ld, peers=%d\n", s.name.c_str(), status_str,
				       s.total_done, s.num_peers);
				all_done &=
				    (s.state == libtorrent::torrent_status::seeding || s.state == libtorrent::torrent_status::finished);
			}
		}
		os_sleep_ms(100);
	}
	printf("tordl: downloads complete\n");
}

int
main(int argc, char *argv[])
{
	sys_info.nprocs = (u32)sysconf(_SC_NPROCESSORS_ONLN);
	sys_info.page_size = (u64)sysconf(_SC_PAGESIZE);
	sys_info.large_page_size = MB(2);
	arena = arena_alloc((ArenaParams){
	    .flags = arena_default_flags, .res_size = arena_default_res_size, .cmt_size = arena_default_cmt_size});
	String8List args = os_args(arena, argc, argv);
	Cmd parsed = cmd_parse(arena, args);
	Temp scratch = temp_begin(arena);
	String8 path = str8_zero();
	if (cmd_has_arg(&parsed, str8_lit("f"))) {
		path = cmd_str(&parsed, str8_lit("f"));
	}
	String8 data = str8_zero();
	if (path.len > 0) {
		data = os_read_file(arena, path);
		if (data.len == 0) {
			fprintf(stderr, "tordl: failed to read file: %s\n", path.str);
			temp_end(scratch);
			arena_release(arena);
			return 1;
		}
	} else {
		u8 buf[4096] = {0};
		for (;;) {
			ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
			if (n <= 0) {
				break;
			}
			String8 chunk = str8(buf, (u64)n);
			data = push_str8_cat(scratch.a, data, chunk);
		}
	}
	if (data.len == 0) {
		fprintf(stderr, "tordl: no data read\n");
		temp_end(scratch);
		arena_release(arena);
		return 1;
	}
	String8Array torrents = parse_torrents(scratch.a, data);
	if (torrents.cnt == 0) {
		fprintf(stderr, "tordl: no torrents found\n");
		temp_end(scratch);
		arena_release(arena);
		return 1;
	}
	download_torrents(torrents);
	temp_end(scratch);
	arena_release(arena);
	return 0;
}
