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
	u64 fd        = socketdial(na, local);
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
entry_point(CmdLine *cmd_line)
{
	Temp scratch  = scratch_begin(NULL, 0);
	String8 addr  = str8_zero();
	String8 aname = str8_zero();
	if (cmd_line_has_argument(cmd_line, str8_lit("a")))
	{
		addr = cmd_line_string(cmd_line, str8_lit("a"));
	}
	if (cmd_line_has_argument(cmd_line, str8_lit("A")))
	{
		aname = cmd_line_string(cmd_line, str8_lit("A"));
	}
	if (cmd_line->inputs.node_count < 2)
	{
		usage();
		return;
	}
	String8 cmd = cmd_line->inputs.first->string;

	// create name...
	if (str8_match(cmd, str8_lit("create"), 0))
	{
		Cfsys *fs = fsconnect(scratch.arena, addr, aname);
		if (fs == NULL)
		{
			return;
		}
		for (String8Node *node = cmd_line->inputs.first->next; node != NULL; node = node->next)
		{
			String8 name = node->string;
			Cfid *fid    = fscreate(scratch.arena, fs, name, OREAD, 0666);
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
	}
	// read name
	else if (str8_match(cmd, str8_lit("read"), 0))
	{
		String8 name = cmd_line->inputs.first->next->string;
		Cfsys *fs    = fsconnect(scratch.arena, addr, aname);
		if (fs == NULL)
		{
			return;
		}
		Cfid *fid = fs9open(scratch.arena, fs, name, OREAD);
		if (fid == NULL)
		{
			fprintf(stderr, "9p: failed to open '%.*s'\n", str8_varg(name));
			fs9unmount(scratch.arena, fs);
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
	}
	// write name
	else if (str8_match(cmd, str8_lit("write"), 0))
	{
		String8 name = cmd_line->inputs.first->next->string;
		Cfsys *fs    = fsconnect(scratch.arena, addr, aname);
		if (fs == NULL)
		{
			return;
		}
		Cfid *fid = fs9open(scratch.arena, fs, name, OWRITE | OTRUNC);
		if (fid == NULL)
		{
			fprintf(stderr, "9p: failed to open '%.*s'\n", str8_varg(name));
			fs9unmount(scratch.arena, fs);
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
	}
	// remove name...
	else if (str8_match(cmd, str8_lit("remove"), 0))
	{
		Cfsys *fs = fsconnect(scratch.arena, addr, aname);
		if (fs == NULL)
		{
			return;
		}
		for (String8Node *node = cmd_line->inputs.first->next; node != NULL; node = node->next)
		{
			String8 name = node->string;
			if (fsremove(scratch.arena, fs, name) < 0)
			{
				fprintf(stderr, "9p: failed to remove '%.*s'\n", str8_varg(name));
			}
		}
		fs9unmount(scratch.arena, fs);
	}
	// stat name
	else if (str8_match(cmd, str8_lit("stat"), 0))
	{
		String8 name = cmd_line->inputs.first->next->string;
		Cfsys *fs    = fsconnect(scratch.arena, addr, aname);
		if (fs == NULL)
		{
			return;
		}
		Dir d = fsdirstat(scratch.arena, fs, name);
		if (d.name.size == 0)
		{
			fprintf(stderr, "9p: failed to stat '%.*s'\n", str8_varg(name));
			fs9unmount(scratch.arena, fs);
			return;
		}
		printf("%.*s %lu %d %.*s %.*s\n", str8_varg(d.name), d.len, d.mtime, str8_varg(d.uid), str8_varg(d.gid));
		fs9unmount(scratch.arena, fs);
	}
	// ls name...
	else if (str8_match(cmd, str8_lit("ls"), 0))
	{
		Cfsys *fs = fsconnect(scratch.arena, addr, aname);
		if (fs == NULL)
		{
			return;
		}
		String8Node *namenode = cmd_line->inputs.first->next;
		if (namenode == NULL)
		{
			String8 name = str8_lit(".");
			Dir d        = fsdirstat(scratch.arena, fs, name);
			if (d.name.size == 0)
			{
				fprintf(stderr, "9p: failed to stat '%.*s'\n", str8_varg(name));
				fs9unmount(scratch.arena, fs);
				return;
			}
			if (d.mode & DMDIR)
			{
				Cfid *fid = fs9open(scratch.arena, fs, name, OREAD);
				if (fid == NULL)
				{
					fprintf(stderr, "9p: failed to open directory '%.*s'\n", str8_varg(name));
					fs9unmount(scratch.arena, fs);
					return;
				}
				Dirlist list = {0};
				if (fsdirreadall(scratch.arena, fid, &list) < 0)
				{
					fprintf(stderr, "9p: failed to read directory '%.*s'\n", str8_varg(name));
					fsclose(scratch.arena, fid);
					fs9unmount(scratch.arena, fs);
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
				Dir d        = fsdirstat(scratch.arena, fs, name);
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
	}
	else
	{
		fprintf(stderr, "9p: unsupported command '%.*s'\n", str8_varg(cmd));
	}
	scratch_end(scratch);
}
