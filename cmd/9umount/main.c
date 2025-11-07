#include <mntent.h>
#include <pwd.h>
#include <sys/mount.h>

// clang-format off
#include "base/inc.h"
#include "base/inc.c"
// clang-format on

static b32
mountedby(Arena *a, String8 mntopts, String8 user)
{
	String8List opts = str8_split(a, mntopts, (u8 *)",", 1, 0);
	for(String8Node *node = opts.first; node != 0; node = node->next)
	{
		String8 opt = node->string;
		if(str8_find_needle(opt, 0, str8_lit("uname="), 0) == 0)
		{
			String8 uname = str8_skip(opt, 6);
			return str8_match(uname, user, 0);
		}
	}
	return 0;
}

static void
entry_point(CmdLine *cmd_line)
{
	Temp scratch = scratch_begin(0, 0);
	Log *log = log_alloc();
	log_select(log);
	log_scope_begin();

	if(cmd_line->inputs.node_count == 0)
	{
		log_error(str8_lit("usage: 9umount mtpt...\n"));
	}
	else
	{
		uid_t uid = getuid();
		struct passwd *pw = getpwuid(uid);
		if(pw == 0)
		{
			log_errorf("9umount: unknown uid %d\n", uid);
		}
		else
		{
			for(String8Node *node = cmd_line->inputs.first; node != 0; node = node->next)
			{
				String8 mtpt = node->string;
				String8 path = os_full_path_from_path(scratch.arena, mtpt);
				if(path.size == 0)
				{
					log_errorf("9umount: %S: %s\n", mtpt, strerror(errno));
					continue;
				}
				FILE *fp = setmntent("/proc/mounts", "r");
				if(fp == 0)
				{
					log_errorf("9umount: could not open /proc/mounts: %s\n", strerror(errno));
					break;
				}
				b32 ok = 0;
				for(;;)
				{
					struct mntent *mnt = getmntent(fp);
					if(mnt == 0)
					{
						break;
					}
					String8 mntdir = str8_cstring(mnt->mnt_dir);
					if(str8_match(path, mntdir, 0))
					{
						ok = 1;
						String8 mnttype = str8_cstring(mnt->mnt_type);
						String8 mntopts = str8_cstring(mnt->mnt_opts);
						String8 homedir = str8_cstring(pw->pw_dir);
						String8 user = str8_cstring(pw->pw_name);
						b32 inhome = (str8_find_needle(mntdir, 0, homedir, 0) == 0);
						if(!inhome && !str8_match(mnttype, str8_lit("9p"), 0))
						{
							log_errorf("9umount: %S: refusing to unmount non-9p fs\n", path);
						}
						else if(!inhome && !mountedby(scratch.arena, mntopts, user))
						{
							log_errorf("9umount: %S: not mounted by you\n", path);
						}
						else if(umount(mnt->mnt_dir))
						{
							log_errorf("9umount: umount %S: %s\n", mntdir, strerror(errno));
						}
						break;
					}
				}
				endmntent(fp);
				if(!ok)
				{
					log_errorf("9umount: %S not found in /proc/mounts\n", path);
				}
			}
		}
	}

	LogScopeResult result = log_scope_end(scratch.arena);
	if(result.strings[LogMsgKind_Error].size > 0)
	{
		fwrite(result.strings[LogMsgKind_Error].str, 1, result.strings[LogMsgKind_Error].size, stderr);
		fflush(stderr);
	}
	scratch_end(scratch);
}
