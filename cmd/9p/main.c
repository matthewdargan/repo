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

readonly static String8 mon[] = {str8litc("Jan"), str8litc("Feb"), str8litc("Mar"), str8litc("Apr"),
                                 str8litc("May"), str8litc("Jun"), str8litc("Jul"), str8litc("Aug"),
                                 str8litc("Sep"), str8litc("Oct"), str8litc("Nov"), str8litc("Dec")};

static void
usage(void)
{
	fprintf(stderr,
	        "usage: 9p [-a address] [-A aname] cmd args...\n"
	        "possible cmds:\n"
	        " create name...\n"
	        " read name\n"
	        " write name\n"
	        " remove name...\n"
	        " stat name\n"
	        " ls [-dlnt] name...\n");
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
	na = netaddr(a, addr, str8lit("tcp"), str8lit("9pfs"));
	if (na.net.len == 0) {
		fprintf(stderr, "9p: failed to parse address '%.*s'\n", str8varg(addr));
		return NULL;
	}
	memset(&local, 0, sizeof local);
	fd = socketdial(na, local);
	if (fd == 0) {
		fprintf(stderr, "9p: dial failed for '%.*s'\n", str8varg(addr));
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
cmd9pcreate(Arena *a, String8 addr, String8 aname, Cmd parsed)
{
	Temp scratch;
	Cfsys *fs;
	Cfid *fid;
	String8node *node;
	String8 name;

	scratch = tempbegin(a);
	fs = fsconnect(scratch.a, addr, aname);
	if (fs == NULL) {
		tempend(scratch);
		return;
	}
	for (node = parsed.inputs.start->next; node != NULL; node = node->next) {
		name = node->str;
		fid = fscreate(scratch.a, fs, name, OREAD, 0666);
		if (fid == NULL)
			fprintf(stderr, "9p: failed to create '%.*s'\n", str8varg(name));
		else
			fsclose(scratch.a, fid);
	}
	fs9unmount(scratch.a, fs);
	tempend(scratch);
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
		fprintf(stderr, "9p: failed to open '%.*s'\n", str8varg(name));
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
cmd9pwrite(Arena *a, String8 addr, String8 aname, String8 name)
{
	Temp scratch;
	Cfsys *fs;
	Cfid *fid;
	u8 *buf;
	s64 n, nwrite;

	scratch = tempbegin(a);
	fs = fsconnect(scratch.a, addr, aname);
	if (fs == NULL) {
		tempend(scratch);
		return;
	}
	fid = fs9open(scratch.a, fs, name, OWRITE | OTRUNC);
	if (fid == NULL) {
		fprintf(stderr, "9p: failed to open '%.*s'\n", str8varg(name));
		fs9unmount(scratch.a, fs);
		tempend(scratch);
		return;
	}
	buf = pusharrnoz(scratch.a, u8, DIRMAX);
	for (;;) {
		n = read(STDIN_FILENO, buf, DIRMAX);
		if (n <= 0)
			break;
		nwrite = fswrite(scratch.a, fid, buf, n);
		if (nwrite != n) {
			fprintf(stderr, "9p: write error\n");
			break;
		}
	}
	if (n < 0)
		fprintf(stderr, "9p: write error: %s\n", strerror(errno));
	fsclose(scratch.a, fid);
	fs9unmount(scratch.a, fs);
	tempend(scratch);
}

static void
cmd9premove(Arena *a, String8 addr, String8 aname, Cmd parsed)
{
	Temp scratch;
	Cfsys *fs;
	String8node *node;
	String8 name;

	scratch = tempbegin(a);
	fs = fsconnect(scratch.a, addr, aname);
	if (fs == NULL) {
		tempend(scratch);
		return;
	}
	for (node = parsed.inputs.start->next; node != NULL; node = node->next) {
		name = node->str;
		if (fsremove(scratch.a, fs, name) < 0)
			fprintf(stderr, "9p: failed to remove '%.*s'\n", str8varg(name));
	}
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
		fprintf(stderr, "9p: failed to stat '%.*s'\n", str8varg(name));
		fs9unmount(scratch.a, fs);
		tempend(scratch);
		return;
	}
	printf("%.*s %lu %d %.*s %.*s\n", str8varg(d.name), d.len, d.mtime, str8varg(d.uid), str8varg(d.gid));
	fs9unmount(scratch.a, fs);
	tempend(scratch);
}

static int
timecmp(const void *va, const void *vb)
{
	Dirnode *a, *b;

	a = (Dirnode *)va;
	b = (Dirnode *)vb;
	if (a->dir.mtime < b->dir.mtime)
		return -1;
	else if (a->dir.mtime > b->dir.mtime)
		return 1;
	else
		return 0;
}

static int
dircmp(const void *va, const void *vb)
{
	Dirnode *a, *b;

	a = (Dirnode *)va;
	b = (Dirnode *)vb;
	return str8cmp(a->dir.name, b->dir.name, 0) ? 1 : -1;
}

static String8
formattime(Arena *a, u32 mtime)
{
	u32 now;
	Datetime dt;

	now = nowunix();
	dt = densetodatetime(mtime);
	if ((now - mtime) < (6 * 30 * 86400))
		return pushstr8f(a, "%s %2d %02d:%02d", mon[dt.mon], dt.day + 1, dt.hour, dt.min);
	else
		return pushstr8f(a, "%s %2d %5d", mon[dt.mon], dt.day + 1, dt.year);
}

static void
printdir(Arena *a, Dir d, b32 lflag)
{
	String8 time;
	char mode[11];

	if (!lflag) {
		printf("%.*s\n", str8varg(d.name));
		return;
	}
	mode[0] = (d.mode & DMDIR) ? 'd' : '-';
	mode[1] = (d.mode & 0400) ? 'r' : '-';
	mode[2] = (d.mode & 0200) ? 'w' : '-';
	mode[3] = (d.mode & 0100) ? 'x' : '-';
	mode[4] = (d.mode & 0040) ? 'r' : '-';
	mode[5] = (d.mode & 0020) ? 'w' : '-';
	mode[6] = (d.mode & 0010) ? 'x' : '-';
	mode[7] = (d.mode & 0004) ? 'r' : '-';
	mode[8] = (d.mode & 0002) ? 'w' : '-';
	mode[9] = (d.mode & 0001) ? 'x' : '-';
	mode[10] = '\0';
	time = formattime(a, d.mtime);
	printf("%s %.*s %.*s %10lu %.*s %.*s\n", mode, str8varg(d.uid), str8varg(d.gid), d.len, str8varg(time),
	       str8varg(d.name));
}

static void
listdir(Arena *a, Cfsys *fs, String8 name, b32 lflag, b32 nflag, b32 tflag)
{
	Temp scratch;
	Cfid *fid;
	Dirlist list;
	Dirnode *node, *arr;
	u64 i;

	scratch = tempbegin(a);
	fid = fs9open(scratch.a, fs, name, OREAD);
	if (fid == NULL) {
		fprintf(stderr, "9p: failed to open directory '%.*s'\n", str8varg(name));
		tempend(scratch);
		return;
	}
	memset(&list, 0, sizeof list);
	if (fsdirreadall(scratch.a, fid, &list) < 0) {
		fprintf(stderr, "9p: failed to read directory '%.*s'\n", str8varg(name));
		fsclose(scratch.a, fid);
		tempend(scratch);
		return;
	}
	fsclose(scratch.a, fid);
	if (list.cnt > 0 && !nflag) {
		arr = pusharrnoz(scratch.a, Dirnode, list.cnt);
		i = 0;
		for (node = list.start; node != NULL; node = node->next) {
			arr[i] = *node;
			i++;
		}
		if (tflag)
			qsort(arr, list.cnt, sizeof(Dirnode), timecmp);
		else
			qsort(arr, list.cnt, sizeof(Dirnode), dircmp);
		for (i = 0; i < list.cnt; i++)
			printdir(scratch.a, arr[i].dir, lflag);
	} else {
		for (node = list.start; node != NULL; node = node->next)
			printdir(scratch.a, node->dir, lflag);
	}
	tempend(scratch);
}

static void
cmd9pls(Arena *a, String8 addr, String8 aname, Cmd parsed)
{
	Temp scratch;
	b32 dflag, lflag, nflag, tflag;
	Cfsys *fs;
	String8node *namenode;
	String8 name;
	Dir d;

	scratch = tempbegin(a);
	dflag = cmdhasflag(&parsed, str8lit("d"));
	lflag = cmdhasflag(&parsed, str8lit("l"));
	nflag = cmdhasflag(&parsed, str8lit("n"));
	tflag = cmdhasflag(&parsed, str8lit("t"));
	fs = fsconnect(scratch.a, addr, aname);
	if (fs == NULL) {
		tempend(scratch);
		return;
	}
	namenode = parsed.inputs.start->next;
	if (namenode == NULL) {
		name = str8lit(".");
		d = fsdirstat(scratch.a, fs, name);
		if (d.name.len == 0) {
			fprintf(stderr, "9p: failed to stat '%.*s'\n", str8varg(name));
			fs9unmount(scratch.a, fs);
			tempend(scratch);
			return;
		}
		if ((d.mode & DMDIR) && !dflag)
			listdir(scratch.a, fs, name, lflag, nflag, tflag);
		else
			printdir(scratch.a, d, lflag);
	} else {
		for (; namenode != NULL; namenode = namenode->next) {
			name = namenode->str;
			d = fsdirstat(scratch.a, fs, name);
			if (d.name.len == 0) {
				fprintf(stderr, "9p: failed to stat '%.*s'\n", str8varg(name));
				continue;
			}
			if ((d.mode & DMDIR) && !dflag)
				listdir(scratch.a, fs, name, lflag, nflag, tflag);
			else
				printdir(scratch.a, d, lflag);
		}
	}
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
	if (str8cmp(cmd, str8lit("create"), 0))
		cmd9pcreate(arena, addr, aname, parsed);
	else if (str8cmp(cmd, str8lit("read"), 0)) {
		name = parsed.inputs.start->next->str;
		cmd9pread(arena, addr, aname, name);
	} else if (str8cmp(cmd, str8lit("write"), 0)) {
		name = parsed.inputs.start->next->str;
		cmd9pwrite(arena, addr, aname, name);
	} else if (str8cmp(cmd, str8lit("remove"), 0))
		cmd9premove(arena, addr, aname, parsed);
	else if (str8cmp(cmd, str8lit("stat"), 0)) {
		name = parsed.inputs.start->next->str;
		cmd9pstat(arena, addr, aname, name);
	} else if (str8cmp(cmd, str8lit("ls"), 0))
		cmd9pls(arena, addr, aname, parsed);
	else {
		fprintf(stderr, "9p: unsupported command '%.*s'\n", str8varg(cmd));
		arenarelease(arena);
		return 1;
	}
	arenarelease(arena);
	return 0;
}
