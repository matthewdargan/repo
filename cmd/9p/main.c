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
	fprintf(stderr,
	        "usage: 9p [-a address] [-A aname] cmd args...\n"
	        "possible cmds:\n"
	        " create name...\n"
	        " read name\n"
	        " write name\n"
	        " remove name...\n"
	        " stat name\n"
	        " ls name...\n");
}

static Cfsys *
fsconnect(Arena *a, String8 addr, String8 aname)
{
	if (addr.len == 0)
	{
		fprintf(stderr, "9p: namespace mounting not implemented\n");
		return NULL;
	}
	Netaddr na = netaddr(a, addr, str8lit("tcp"), str8lit("9pfs"));
	if (na.net.len == 0)
	{
		fprintf(stderr, "9p: failed to parse address '%.*s'\n", str8varg(addr));
		return NULL;
	}
	Netaddr local = {0};
	u64 fd = socketdial(na, local);
	if (fd == 0)
	{
		fprintf(stderr, "9p: dial failed for '%.*s'\n", str8varg(addr));
		return NULL;
	}
	Cfsys *fs = fs9mount(a, fd, aname);
	if (fs == NULL)
	{
		close(fd);
		fprintf(stderr, "9p: mount failed\n");
		return NULL;
	}
	return fs;
}

static void
cmd9pcreate(Arena *a, String8 addr, String8 aname, Cmd parsed)
{
	Temp scratch = tempbegin(a);
	Cfsys *fs = fsconnect(scratch.a, addr, aname);
	if (fs == NULL)
	{
		tempend(scratch);
		return;
	}
	for (String8node *node = parsed.inputs.start->next; node != NULL; node = node->next)
	{
		String8 name = node->str;
		Cfid *fid = fscreate(scratch.a, fs, name, OREAD, 0666);
		if (fid == NULL)
		{
			fprintf(stderr, "9p: failed to create '%.*s'\n", str8varg(name));
		}
		else
		{
			fsclose(scratch.a, fid);
		}
	}
	fs9unmount(scratch.a, fs);
	tempend(scratch);
}

static void
cmd9pread(Arena *a, String8 addr, String8 aname, String8 name)
{
	Temp scratch = tempbegin(a);
	Cfsys *fs = fsconnect(scratch.a, addr, aname);
	if (fs == NULL)
	{
		tempend(scratch);
		return;
	}
	Cfid *fid = fs9open(scratch.a, fs, name, OREAD);
	if (fid == NULL)
	{
		fprintf(stderr, "9p: failed to open '%.*s'\n", str8varg(name));
		fs9unmount(scratch.a, fs);
		tempend(scratch);
		return;
	}
	u8 *buf = pusharrnoz(scratch.a, u8, DIRMAX);
	for (;;)
	{
		s64 n = fsread(scratch.a, fid, buf, DIRMAX);
		if (n <= 0)
		{
			if (n < 0)
			{
				fprintf(stderr, "9p: read error\n");
			}
			break;
		}
		if (write(STDOUT_FILENO, buf, n) != n)
		{
			fprintf(stderr, "9p: write error: %s\n", strerror(errno));
			break;
		}
	}
	fsclose(scratch.a, fid);
	fs9unmount(scratch.a, fs);
	tempend(scratch);
}

static void
cmd9pwrite(Arena *a, String8 addr, String8 aname, String8 name)
{
	Temp scratch = tempbegin(a);
	Cfsys *fs = fsconnect(scratch.a, addr, aname);
	if (fs == NULL)
	{
		tempend(scratch);
		return;
	}
	Cfid *fid = fs9open(scratch.a, fs, name, OWRITE | OTRUNC);
	if (fid == NULL)
	{
		fprintf(stderr, "9p: failed to open '%.*s'\n", str8varg(name));
		fs9unmount(scratch.a, fs);
		tempend(scratch);
		return;
	}
	u8 *buf = pusharrnoz(scratch.a, u8, DIRMAX);
	for (;;)
	{
		s64 n = read(STDIN_FILENO, buf, DIRMAX);
		if (n <= 0)
		{
			if (n < 0)
			{
				fprintf(stderr, "9p: write error: %s\n", strerror(errno));
			}
			break;
		}
		s64 nwrite = fswrite(scratch.a, fid, buf, n);
		if (nwrite != n)
		{
			fprintf(stderr, "9p: write error\n");
			break;
		}
	}
	fsclose(scratch.a, fid);
	fs9unmount(scratch.a, fs);
	tempend(scratch);
}

static void
cmd9premove(Arena *a, String8 addr, String8 aname, Cmd parsed)
{
	Temp scratch = tempbegin(a);
	Cfsys *fs = fsconnect(scratch.a, addr, aname);
	if (fs == NULL)
	{
		tempend(scratch);
		return;
	}
	for (String8node *node = parsed.inputs.start->next; node != NULL; node = node->next)
	{
		String8 name = node->str;
		if (fsremove(scratch.a, fs, name) < 0)
		{
			fprintf(stderr, "9p: failed to remove '%.*s'\n", str8varg(name));
		}
	}
	fs9unmount(scratch.a, fs);
	tempend(scratch);
}

static void
cmd9pstat(Arena *a, String8 addr, String8 aname, String8 name)
{
	Temp scratch = tempbegin(a);
	Cfsys *fs = fsconnect(scratch.a, addr, aname);
	if (fs == NULL)
	{
		tempend(scratch);
		return;
	}
	Dir d = fsdirstat(scratch.a, fs, name);
	if (d.name.len == 0)
	{
		fprintf(stderr, "9p: failed to stat '%.*s'\n", str8varg(name));
		fs9unmount(scratch.a, fs);
		tempend(scratch);
		return;
	}
	printf("%.*s %lu %d %.*s %.*s\n", str8varg(d.name), d.len, d.mtime, str8varg(d.uid), str8varg(d.gid));
	fs9unmount(scratch.a, fs);
	tempend(scratch);
}

static void
cmd9pls(Arena *a, String8 addr, String8 aname, Cmd parsed)
{
	Temp scratch = tempbegin(a);
	Cfsys *fs = fsconnect(scratch.a, addr, aname);
	if (fs == NULL)
	{
		tempend(scratch);
		return;
	}
	String8node *namenode = parsed.inputs.start->next;
	if (namenode == NULL)
	{
		String8 name = str8lit(".");
		Dir d = fsdirstat(scratch.a, fs, name);
		if (d.name.len == 0)
		{
			fprintf(stderr, "9p: failed to stat '%.*s'\n", str8varg(name));
			fs9unmount(scratch.a, fs);
			tempend(scratch);
			return;
		}
		if (d.mode & DMDIR)
		{
			Cfid *fid = fs9open(scratch.a, fs, name, OREAD);
			if (fid == NULL)
			{
				fprintf(stderr, "9p: failed to open directory '%.*s'\n", str8varg(name));
				fs9unmount(scratch.a, fs);
				tempend(scratch);
				return;
			}
			Dirlist list = {0};
			if (fsdirreadall(scratch.a, fid, &list) < 0)
			{
				fprintf(stderr, "9p: failed to read directory '%.*s'\n", str8varg(name));
				fsclose(scratch.a, fid);
				fs9unmount(scratch.a, fs);
				tempend(scratch);
				return;
			}
			fsclose(scratch.a, fid);
			for (Dirnode *node = list.start; node != NULL; node = node->next)
			{
				printf("%.*s\n", str8varg(node->dir.name));
			}
		}
		else
		{
			printf("%.*s\n", str8varg(d.name));
		}
	}
	else
	{
		for (; namenode != NULL; namenode = namenode->next)
		{
			String8 name = namenode->str;
			Dir d = fsdirstat(scratch.a, fs, name);
			if (d.name.len == 0)
			{
				fprintf(stderr, "9p: failed to stat '%.*s'\n", str8varg(name));
				continue;
			}
			if (d.mode & DMDIR)
			{
				Cfid *fid = fs9open(scratch.a, fs, name, OREAD);
				if (fid == NULL)
				{
					fprintf(stderr, "9p: failed to open '%.*s'\n", str8varg(name));
					continue;
				}
				Dirlist list = {0};
				if (fsdirreadall(scratch.a, fid, &list) < 0)
				{
					fprintf(stderr, "9p: failed to read directory '%.*s'\n", str8varg(name));
					fsclose(scratch.a, fid);
					continue;
				}
				fsclose(scratch.a, fid);
				for (Dirnode *node = list.start; node != NULL; node = node->next)
				{
					printf("%.*s\n", str8varg(node->dir.name));
				}
			}
			else
			{
				printf("%.*s\n", str8varg(d.name));
			}
		}
	}
	fs9unmount(scratch.a, fs);
	tempend(scratch);
}

int
main(int argc, char *argv[])
{
	sysinfo = (Sysinfo){.nprocs = sysconf(_SC_NPROCESSORS_ONLN), .pagesz = sysconf(_SC_PAGESIZE), .lpagesz = 0x200000};
	Arenaparams ap = {.flags = arenaflags, .ressz = arenaressz, .cmtsz = arenacmtsz};
	Arena *arena = arenaalloc(ap);
	String8list args = osargs(arena, argc, argv);
	Cmd parsed = cmdparse(arena, args);
	String8 addr = str8zero();
	String8 aname = str8zero();
	if (cmdhasarg(&parsed, str8lit("a")))
	{
		addr = cmdstr(&parsed, str8lit("a"));
	}
	if (cmdhasarg(&parsed, str8lit("A")))
	{
		aname = cmdstr(&parsed, str8lit("A"));
	}
	if (parsed.inputs.nnode < 2)
	{
		usage();
		arenarelease(arena);
		return 1;
	}
	String8 cmd = parsed.inputs.start->str;
	if (str8cmp(cmd, str8lit("create"), 0))
	{
		cmd9pcreate(arena, addr, aname, parsed);
	}
	else if (str8cmp(cmd, str8lit("read"), 0))
	{
		String8 name = parsed.inputs.start->next->str;
		cmd9pread(arena, addr, aname, name);
	}
	else if (str8cmp(cmd, str8lit("write"), 0))
	{
		String8 name = parsed.inputs.start->next->str;
		cmd9pwrite(arena, addr, aname, name);
	}
	else if (str8cmp(cmd, str8lit("remove"), 0))
	{
		cmd9premove(arena, addr, aname, parsed);
	}
	else if (str8cmp(cmd, str8lit("stat"), 0))
	{
		String8 name = parsed.inputs.start->next->str;
		cmd9pstat(arena, addr, aname, name);
	}
	else if (str8cmp(cmd, str8lit("ls"), 0))
	{
		cmd9pls(arena, addr, aname, parsed);
	}
	else
	{
		fprintf(stderr, "9p: unsupported command '%.*s'\n", str8varg(cmd));
		arenarelease(arena);
		return 1;
	}
	arenarelease(arena);
	return 0;
}
