#include <pwd.h>
#include <sys/mount.h>

/* clang-format off */
#include "base/inc.h"
#include "9p/fcall.h"
#include "9p/9pclient.h"
#include "base/inc.c"
#include "9p/fcall.c"
#include "9p/9pclient.c"
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
	if (addr.size == 0)
	{
		fprintf(stderr, "9p: namespace mounting not implemented\n");
		return NULL;
	}
	Netaddr na = netaddr(a, addr, str8_lit("tcp"), str8_lit("9pfs"));
	if (na.net.size == 0)
	{
		fprintf(stderr, "9p: failed to parse address '%.*s'\n", str8_varg(addr));
		return NULL;
	}
	Netaddr local = {0};
	u64 fd = socketdial(na, local);
	if (fd == 0)
	{
		fprintf(stderr, "9p: dial failed for '%.*s'\n", str8_varg(addr));
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
	Temp scratch = temp_begin(a);
	Cfsys *fs = fsconnect(scratch.arena, addr, aname);
	if (fs == NULL)
	{
		temp_end(scratch);
		return;
	}
	for (String8Node *node = parsed.inputs.first->next; node != NULL; node = node->next)
	{
		String8 name = node->string;
		Cfid *fid = fscreate(scratch.arena, fs, name, OREAD, 0666);
		if (fid == NULL)
		{
			fprintf(stderr, "9p: failed to create '%.*s'\n", str8_varg(name));
		}
		else
		{
			fsclose(scratch.arena, fid);
		}
	}
	fs9unmount(scratch.arena, fs);
	temp_end(scratch);
}

static void
cmd9pread(Arena *a, String8 addr, String8 aname, String8 name)
{
	Temp scratch = temp_begin(a);
	Cfsys *fs = fsconnect(scratch.arena, addr, aname);
	if (fs == NULL)
	{
		temp_end(scratch);
		return;
	}
	Cfid *fid = fs9open(scratch.arena, fs, name, OREAD);
	if (fid == NULL)
	{
		fprintf(stderr, "9p: failed to open '%.*s'\n", str8_varg(name));
		fs9unmount(scratch.arena, fs);
		temp_end(scratch);
		return;
	}
	u8 *buf = push_array_no_zero(scratch.arena, u8, DIRMAX);
	for (;;)
	{
		s64 n = fsread(scratch.arena, fid, buf, DIRMAX);
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
	fsclose(scratch.arena, fid);
	fs9unmount(scratch.arena, fs);
	temp_end(scratch);
}

static void
cmd9pwrite(Arena *a, String8 addr, String8 aname, String8 name)
{
	Temp scratch = temp_begin(a);
	Cfsys *fs = fsconnect(scratch.arena, addr, aname);
	if (fs == NULL)
	{
		temp_end(scratch);
		return;
	}
	Cfid *fid = fs9open(scratch.arena, fs, name, OWRITE | OTRUNC);
	if (fid == NULL)
	{
		fprintf(stderr, "9p: failed to open '%.*s'\n", str8_varg(name));
		fs9unmount(scratch.arena, fs);
		temp_end(scratch);
		return;
	}
	u8 *buf = push_array_no_zero(scratch.arena, u8, DIRMAX);
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
		s64 nwrite = fswrite(scratch.arena, fid, buf, n);
		if (nwrite != n)
		{
			fprintf(stderr, "9p: write error\n");
			break;
		}
	}
	fsclose(scratch.arena, fid);
	fs9unmount(scratch.arena, fs);
	temp_end(scratch);
}

static void
cmd9premove(Arena *a, String8 addr, String8 aname, Cmd parsed)
{
	Temp scratch = temp_begin(a);
	Cfsys *fs = fsconnect(scratch.arena, addr, aname);
	if (fs == NULL)
	{
		temp_end(scratch);
		return;
	}
	for (String8Node *node = parsed.inputs.first->next; node != NULL; node = node->next)
	{
		String8 name = node->string;
		if (fsremove(scratch.arena, fs, name) < 0)
		{
			fprintf(stderr, "9p: failed to remove '%.*s'\n", str8_varg(name));
		}
	}
	fs9unmount(scratch.arena, fs);
	temp_end(scratch);
}

static void
cmd9pstat(Arena *a, String8 addr, String8 aname, String8 name)
{
	Temp scratch = temp_begin(a);
	Cfsys *fs = fsconnect(scratch.arena, addr, aname);
	if (fs == NULL)
	{
		temp_end(scratch);
		return;
	}
	Dir d = fsdirstat(scratch.arena, fs, name);
	if (d.name.size == 0)
	{
		fprintf(stderr, "9p: failed to stat '%.*s'\n", str8_varg(name));
		fs9unmount(scratch.arena, fs);
		temp_end(scratch);
		return;
	}
	printf("%.*s %lu %d %.*s %.*s\n", str8_varg(d.name), d.len, d.mtime, str8_varg(d.uid), str8_varg(d.gid));
	fs9unmount(scratch.arena, fs);
	temp_end(scratch);
}

static void
cmd9pls(Arena *a, String8 addr, String8 aname, Cmd parsed)
{
	Temp scratch = temp_begin(a);
	Cfsys *fs = fsconnect(scratch.arena, addr, aname);
	if (fs == NULL)
	{
		temp_end(scratch);
		return;
	}
	String8Node *namenode = parsed.inputs.first->next;
	if (namenode == NULL)
	{
		String8 name = str8_lit(".");
		Dir d = fsdirstat(scratch.arena, fs, name);
		if (d.name.size == 0)
		{
			fprintf(stderr, "9p: failed to stat '%.*s'\n", str8_varg(name));
			fs9unmount(scratch.arena, fs);
			temp_end(scratch);
			return;
		}
		if (d.mode & DMDIR)
		{
			Cfid *fid = fs9open(scratch.arena, fs, name, OREAD);
			if (fid == NULL)
			{
				fprintf(stderr, "9p: failed to open directory '%.*s'\n", str8_varg(name));
				fs9unmount(scratch.arena, fs);
				temp_end(scratch);
				return;
			}
			Dirlist list = {0};
			if (fsdirreadall(scratch.arena, fid, &list) < 0)
			{
				fprintf(stderr, "9p: failed to read directory '%.*s'\n", str8_varg(name));
				fsclose(scratch.arena, fid);
				fs9unmount(scratch.arena, fs);
				temp_end(scratch);
				return;
			}
			fsclose(scratch.arena, fid);
			for (Dirnode *node = list.start; node != NULL; node = node->next)
			{
				printf("%.*s\n", str8_varg(node->dir.name));
			}
		}
		else
		{
			printf("%.*s\n", str8_varg(d.name));
		}
	}
	else
	{
		for (; namenode != NULL; namenode = namenode->next)
		{
			String8 name = namenode->string;
			Dir d = fsdirstat(scratch.arena, fs, name);
			if (d.name.size == 0)
			{
				fprintf(stderr, "9p: failed to stat '%.*s'\n", str8_varg(name));
				continue;
			}
			if (d.mode & DMDIR)
			{
				Cfid *fid = fs9open(scratch.arena, fs, name, OREAD);
				if (fid == NULL)
				{
					fprintf(stderr, "9p: failed to open '%.*s'\n", str8_varg(name));
					continue;
				}
				Dirlist list = {0};
				if (fsdirreadall(scratch.arena, fid, &list) < 0)
				{
					fprintf(stderr, "9p: failed to read directory '%.*s'\n", str8_varg(name));
					fsclose(scratch.arena, fid);
					continue;
				}
				fsclose(scratch.arena, fid);
				for (Dirnode *node = list.start; node != NULL; node = node->next)
				{
					printf("%.*s\n", str8_varg(node->dir.name));
				}
			}
			else
			{
				printf("%.*s\n", str8_varg(d.name));
			}
		}
	}
	fs9unmount(scratch.arena, fs);
	temp_end(scratch);
}

int
main(int argc, char *argv[])
{
	OS_SystemInfo *sysinfo = os_get_system_info();
	sysinfo->logical_processor_count = sysconf(_SC_NPROCESSORS_ONLN);
	sysinfo->page_size = sysconf(_SC_PAGESIZE);
	sysinfo->large_page_size = 0x200000;
	Arena *arena = arena_alloc();
	String8List args = os_args(arena, argc, argv);
	Cmd parsed = cmdparse(arena, args);
	String8 addr = str8_zero();
	String8 aname = str8_zero();
	if (cmdhasarg(&parsed, str8_lit("a")))
	{
		addr = cmdstr(&parsed, str8_lit("a"));
	}
	if (cmdhasarg(&parsed, str8_lit("A")))
	{
		aname = cmdstr(&parsed, str8_lit("A"));
	}
	if (parsed.inputs.node_count < 2)
	{
		usage();
		arena_release(arena);
		return 1;
	}
	String8 cmd = parsed.inputs.first->string;
	if (str8_match(cmd, str8_lit("create"), 0))
	{
		cmd9pcreate(arena, addr, aname, parsed);
	}
	else if (str8_match(cmd, str8_lit("read"), 0))
	{
		String8 name = parsed.inputs.first->next->string;
		cmd9pread(arena, addr, aname, name);
	}
	else if (str8_match(cmd, str8_lit("write"), 0))
	{
		String8 name = parsed.inputs.first->next->string;
		cmd9pwrite(arena, addr, aname, name);
	}
	else if (str8_match(cmd, str8_lit("remove"), 0))
	{
		cmd9premove(arena, addr, aname, parsed);
	}
	else if (str8_match(cmd, str8_lit("stat"), 0))
	{
		String8 name = parsed.inputs.first->next->string;
		cmd9pstat(arena, addr, aname, name);
	}
	else if (str8_match(cmd, str8_lit("ls"), 0))
	{
		cmd9pls(arena, addr, aname, parsed);
	}
	else
	{
		fprintf(stderr, "9p: unsupported command '%.*s'\n", str8_varg(cmd));
		arena_release(arena);
		return 1;
	}
	arena_release(arena);
	return 0;
}
