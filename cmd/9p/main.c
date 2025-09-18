#include <dirent.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* clang-format off */
#include "libu/u.h"
#include "libu/arena.h"
#include "libu/string.h"
#include "libu/cmd.h"
#include "libu/os.h"
#include "libu/socket.h"
#include "lib9p/fcall.h"
#include "lib9p/9pclient.h"
#include "libu/u.c"
#include "libu/arena.c"
#include "libu/string.c"
#include "libu/cmd.c"
#include "libu/os.c"
#include "libu/socket.c"
#include "lib9p/fcall.c"
#include "lib9p/9pclient.c"
/* clang-format on */

static void
usage(void)
{
	fprintf(stderr, "usage: 9p [-a address] [-A aname] cmd args...\n");
	fprintf(stderr, "possible cmds:\n");
	fprintf(stderr, " read name\n");
	fprintf(stderr, " write [-l] name\n");
	fprintf(stderr, " stat name\n");
	fprintf(stderr, " rdwr name\n");
	fprintf(stderr, " ls [-dlnt] name\n");
}

static Cfsys *
fsconnect(Arena *a, String8 addr, String8 aname)
{
	Netaddr na, local;
	u64 fd;
	Cfsys *fs;

	if (addr.len == 0) {
		fprintf(stderr, "9p: namespace mounting not implemented\n");
		return NULL;
	}
	na = netaddr(a, addr, str8lit("tcp"), str8lit("9fs"));
	if (na.net.len == 0) {
		fprintf(stderr, "9p: failed to parse address '%.*s'\n", (int)addr.len, addr.str);
		return NULL;
	}
	memset(&local, 0, sizeof local);
	fd = socketdial(na, local);
	if (fd == 0) {
		fprintf(stderr, "9p: dial failed for '%.*s'\n", (int)addr.len, addr.str);
		return NULL;
	}
	fs = fs9mount(a, fd, aname);
	if (fs == NULL) {
		close(fd);
		fprintf(stderr, "9p: mount failed\n");
		return NULL;
	}
	return fs;
}

static void
cmd9pread(Arena *a, String8 addr, String8 aname, String8 name)
{
	Temp scratch;
	Cfsys *fs;
	Cfid *fid;
	u8 *buf;
	s64 n;

	scratch = tempbegin(a);
	fs = fsconnect(scratch.a, addr, aname);
	if (fs == NULL) {
		tempend(scratch);
		return;
	}
	fid = fs9open(scratch.a, fs, name, OREAD);
	if (fid == NULL) {
		fprintf(stderr, "9p: failed to open '%.*s'\n", (int)name.len, name.str);
		fs9unmount(scratch.a, fs);
		tempend(scratch);
		return;
	}
	buf = pusharrnoz(scratch.a, u8, DIRMAX);
	for (;;) {
		n = fsread(scratch.a, fid, buf, DIRMAX);
		if (n <= 0)
			break;
		if (write(STDOUT_FILENO, buf, n) != n) {
			fprintf(stderr, "9p: write error: %s\n", strerror(errno));
			break;
		}
	}
	if (n < 0)
		fprintf(stderr, "9p: read error\n");
	fsclose(scratch.a, fid);
	fs9unmount(scratch.a, fs);
	tempend(scratch);
}

static void
cmd9pstat(Arena *a, String8 addr, String8 aname, String8 name)
{
	Temp scratch;
	Cfsys *fs;
	Dir d;

	scratch = tempbegin(a);
	fs = fsconnect(scratch.a, addr, aname);
	if (fs == NULL) {
		tempend(scratch);
		return;
	}
	d = fsdirstat(scratch.a, fs, name);
	if (d.name.len == 0) {
		fprintf(stderr, "9p: failed to stat '%.*s'\n", (int)name.len, name.str);
		fs9unmount(scratch.a, fs);
		tempend(scratch);
		return;
	}
	printf("%.*s %lld %d %.*s %.*s\n", (int)d.name.len, d.name.str, d.len, d.mtime, (int)d.uid.len, d.uid.str,
	       (int)d.gid.len, d.gid.str);
	fs9unmount(scratch.a, fs);
	tempend(scratch);
}

int
main(int argc, char *argv[])
{
	Arenaparams ap;
	Arena *arena;
	String8list args;
	Cmd parsed;
	String8 addr, aname, cmd, name;

	sysinfo.nprocs = sysconf(_SC_NPROCESSORS_ONLN);
	sysinfo.pagesz = sysconf(_SC_PAGESIZE);
	sysinfo.lpagesz = 0x200000;
	ap.flags = arenaflags;
	ap.ressz = arenaressz;
	ap.cmtsz = arenacmtsz;
	arena = arenaalloc(ap);
	args = osargs(arena, argc, argv);
	parsed = cmdparse(arena, args);
	addr = str8zero();
	aname = str8zero();
	if (cmdhasarg(&parsed, str8lit("a")))
		addr = cmdstr(&parsed, str8lit("a"));
	if (cmdhasarg(&parsed, str8lit("A")))
		aname = cmdstr(&parsed, str8lit("A"));
	if (parsed.inputs.nnode < 2) {
		usage();
		arenarelease(arena);
		return 1;
	}
	cmd = parsed.inputs.start->str;
	name = parsed.inputs.start->next->str;
	if (str8cmp(cmd, str8lit("read"), 0))
		cmd9pread(arena, addr, aname, name);
	else if (str8cmp(cmd, str8lit("stat"), 0))
		cmd9pstat(arena, addr, aname, name);
	else {
		fprintf(stderr, "9p: unsupported command '%.*s'\n", (int)cmd.len, cmd.str);
		arenarelease(arena);
		return 1;
	}
	arenarelease(arena);
	return 0;
}
