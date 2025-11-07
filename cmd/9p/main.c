#include <pwd.h>
#include <sys/mount.h>

/* clang-format off */
#include "base/inc.h"
#include "9p/inc.h"
#include "base/inc.c"
#include "9p/inc.c"
/* clang-format on */

static void
usage(void)
{
	log_error(str8_lit("usage: 9p [-a address] [-A aname] cmd args...\n"
	                   "possible cmds:\n"
	                   " create name...\n"
	                   " read name\n"
	                   " write name\n"
	                   " remove name...\n"
	                   " stat name\n"
	                   " ls name...\n"));
}

static Cfsys *
fsconnect(Arena *a, String8 addr, String8 aname)
{
	if(addr.size == 0)
	{
		log_error(str8_lit("9p: namespace mounting not implemented\n"));
		return 0;
	}
	Netaddr na = netaddr(a, addr, str8_lit("tcp"), str8_lit("9pfs"));
	if(na.net.size == 0)
	{
		log_errorf("9p: failed to parse address '%S'\n", addr);
		return 0;
	}
	Netaddr local = {0};
	u64 fd = socketdial(na, local);
	if(fd == 0)
	{
		log_errorf("9p: dial failed for '%S'\n", addr);
		return 0;
	}
	Cfsys *fs = fs9mount(a, fd, aname);
	if(fs == 0)
	{
		close(fd);
		log_error(str8_lit("9p: mount failed\n"));
		return 0;
	}
	return fs;
}

static void
entry_point(CmdLine *cmd_line)
{
	Temp scratch = scratch_begin(0, 0);
	Log *log = log_alloc();
	log_select(log);
	log_scope_begin();
	String8 addr = str8_zero();
	String8 aname = str8_zero();

	if(cmd_line_has_argument(cmd_line, str8_lit("a")))
	{
		addr = cmd_line_string(cmd_line, str8_lit("a"));
	}
	if(cmd_line_has_argument(cmd_line, str8_lit("A")))
	{
		aname = cmd_line_string(cmd_line, str8_lit("A"));
	}
	if(cmd_line->inputs.node_count < 1)
	{
		usage();
	}
	else
	{
		String8 cmd = cmd_line->inputs.first->string;

		// create name...
		if(str8_match(cmd, str8_lit("create"), 0))
		{
			Cfsys *fs = fsconnect(scratch.arena, addr, aname);
			if(fs != 0)
			{
				for(String8Node *node = cmd_line->inputs.first->next; node != 0; node = node->next)
				{
					String8 name = node->string;
					Cfid *fid = fscreate(scratch.arena, fs, name, OpenFlag_Read, 0666);
					if(fid == 0)
					{
						log_errorf("9p: failed to create '%S'\n", name);
					}
					else
					{
						fsclose(scratch.arena, fid);
					}
				}
				fs9unmount(scratch.arena, fs);
			}
		}
		// read name
		else if(str8_match(cmd, str8_lit("read"), 0))
		{
			String8 name = cmd_line->inputs.first->next->string;
			Cfsys *fs = fsconnect(scratch.arena, addr, aname);
			if(fs != 0)
			{
				Cfid *fid = fs9open(scratch.arena, fs, name, OpenFlag_Read);
				if(fid == 0)
				{
					log_errorf("9p: failed to open '%S'\n", name);
				}
				else
				{
					u8 *buf = push_array_no_zero(scratch.arena, u8, DIR_ENTRY_MAX);
					for(;;)
					{
						s64 n = fsread(scratch.arena, fid, buf, DIR_ENTRY_MAX);
						if(n <= 0)
						{
							if(n < 0)
							{
								log_error(str8_lit("9p: read error\n"));
							}
							break;
						}
						if(write(STDOUT_FILENO, buf, n) != n)
						{
							log_errorf("9p: write error: %s\n", strerror(errno));
							break;
						}
					}
					fsclose(scratch.arena, fid);
				}
				fs9unmount(scratch.arena, fs);
			}
		}
		// write name
		else if(str8_match(cmd, str8_lit("write"), 0))
		{
			String8 name = cmd_line->inputs.first->next->string;
			Cfsys *fs = fsconnect(scratch.arena, addr, aname);
			if(fs != 0)
			{
				Cfid *fid = fs9open(scratch.arena, fs, name, OpenFlag_Write | OpenFlag_Truncate);
				if(fid == 0)
				{
					log_errorf("9p: failed to open '%S'\n", name);
				}
				else
				{
					u8 *buf = push_array_no_zero(scratch.arena, u8, DIR_ENTRY_MAX);
					for(;;)
					{
						s64 n = read(STDIN_FILENO, buf, DIR_ENTRY_MAX);
						if(n <= 0)
						{
							if(n < 0)
							{
								log_errorf("9p: write error: %s\n", strerror(errno));
							}
							break;
						}
						s64 nwrite = fswrite(scratch.arena, fid, buf, n);
						if(nwrite != n)
						{
							log_error(str8_lit("9p: write error\n"));
							break;
						}
					}
					fsclose(scratch.arena, fid);
				}
				fs9unmount(scratch.arena, fs);
			}
		}
		// remove name...
		else if(str8_match(cmd, str8_lit("remove"), 0))
		{
			Cfsys *fs = fsconnect(scratch.arena, addr, aname);
			if(fs != 0)
			{
				for(String8Node *node = cmd_line->inputs.first->next; node != 0; node = node->next)
				{
					String8 name = node->string;
					if(fsremove(scratch.arena, fs, name) < 0)
					{
						log_errorf("9p: failed to remove '%S'\n", name);
					}
				}
				fs9unmount(scratch.arena, fs);
			}
		}
		// stat name
		else if(str8_match(cmd, str8_lit("stat"), 0))
		{
			String8 name = cmd_line->inputs.first->next->string;
			Cfsys *fs = fsconnect(scratch.arena, addr, aname);
			if(fs != 0)
			{
				Dir d = fsdirstat(scratch.arena, fs, name);
				if(d.name.size == 0)
				{
					log_errorf("9p: failed to stat '%S'\n", name);
				}
				else
				{
					log_infof("%S %llu %u %S %S\n", d.name, d.length, d.modify_time, d.user_id, d.group_id);
				}
				fs9unmount(scratch.arena, fs);
			}
		}
		// ls name...
		else if(str8_match(cmd, str8_lit("ls"), 0))
		{
			Cfsys *fs = fsconnect(scratch.arena, addr, aname);
			if(fs != 0)
			{
				String8Node *namenode = cmd_line->inputs.first->next;
				if(namenode == 0)
				{
					String8 name = str8_lit(".");
					Dir d = fsdirstat(scratch.arena, fs, name);
					if(d.name.size == 0)
					{
						log_errorf("9p: failed to stat '%S'\n", name);
					}
					else if(d.mode & ModeFlag_Directory)
					{
						Cfid *fid = fs9open(scratch.arena, fs, name, OpenFlag_Read);
						if(fid == 0)
						{
							log_errorf("9p: failed to open directory '%S'\n", name);
						}
						else
						{
							DirList list = {0};
							if(fsdirreadall(scratch.arena, fid, &list) < 0)
							{
								log_errorf("9p: failed to read directory '%S'\n", name);
							}
							else
							{
								for(DirNode *node = list.first; node != 0; node = node->next)
								{
									log_infof("%S\n", node->dir.name);
								}
							}
							fsclose(scratch.arena, fid);
						}
					}
					else
					{
						log_infof("%S\n", d.name);
					}
				}
				else
				{
					for(; namenode != 0; namenode = namenode->next)
					{
						String8 name = namenode->string;
						Dir d = fsdirstat(scratch.arena, fs, name);
						if(d.name.size == 0)
						{
							log_errorf("9p: failed to stat '%S'\n", name);
							continue;
						}
						if(d.mode & ModeFlag_Directory)
						{
							Cfid *fid = fs9open(scratch.arena, fs, name, OpenFlag_Read);
							if(fid == 0)
							{
								log_errorf("9p: failed to open '%S'\n", name);
								continue;
							}
							DirList list = {0};
							if(fsdirreadall(scratch.arena, fid, &list) < 0)
							{
								log_errorf("9p: failed to read directory '%S'\n", name);
								fsclose(scratch.arena, fid);
								continue;
							}
							fsclose(scratch.arena, fid);
							for(DirNode *node = list.first; node != 0; node = node->next)
							{
								log_infof("%S\n", node->dir.name);
							}
						}
						else
						{
							log_infof("%S\n", d.name);
						}
					}
				}
				fs9unmount(scratch.arena, fs);
			}
		}
		else
		{
			log_errorf("9p: unsupported command '%S'\n", cmd);
		}
	}

	LogScopeResult result = log_scope_end(scratch.arena);
	if(result.strings[LogMsgKind_Info].size > 0)
	{
		fwrite(result.strings[LogMsgKind_Info].str, 1, result.strings[LogMsgKind_Info].size, stdout);
		fflush(stdout);
	}
	if(result.strings[LogMsgKind_Error].size > 0)
	{
		fwrite(result.strings[LogMsgKind_Error].str, 1, result.strings[LogMsgKind_Error].size, stderr);
		fflush(stderr);
	}
	scratch_end(scratch);
}
