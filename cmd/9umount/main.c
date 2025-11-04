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
	for (String8Node *node = opts.first; node != NULL; node = node->next)
	{
		String8 opt = node->string;
		if (str8_find_needle(opt, 0, str8_lit("uname="), 0) == 0)
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
	Temp scratch = scratch_begin(NULL, 0);
	if (cmd_line->inputs.node_count == 0)
	{
		fprintf(stderr, "usage: 9umount mtpt...\n");
		return;
	}
	uid_t uid         = getuid();
	struct passwd *pw = getpwuid(uid);
	if (pw == NULL)
	{
		fprintf(stderr, "9umount: unknown uid %d\n", uid);
		return;
	}
	for (String8Node *node = cmd_line->inputs.first; node != NULL; node = node->next)
	{
		String8 mtpt = node->string;
		String8 path = os_full_path_from_path(scratch.arena, mtpt);
		if (path.size == 0)
		{
			fprintf(stderr, "9umount: %.*s: %s\n", str8_varg(mtpt), strerror(errno));
			continue;
		}
		FILE *fp = setmntent("/proc/mounts", "r");
		if (fp == NULL)
		{
			fprintf(stderr, "9umount: could not open /proc/mounts: %s\n", strerror(errno));
			break;
		}
		b32 ok = 0;
		for (;;)
		{
			struct mntent *mnt = getmntent(fp);
			if (mnt == NULL)
			{
				break;
			}
			String8 mntdir = str8_cstring(mnt->mnt_dir);
			if (str8_match(path, mntdir, 0))
			{
				ok              = 1;
				String8 mnttype = str8_cstring(mnt->mnt_type);
				String8 mntopts = str8_cstring(mnt->mnt_opts);
				String8 homedir = str8_cstring(pw->pw_dir);
				String8 user    = str8_cstring(pw->pw_name);
				b32 inhome      = (str8_find_needle(mntdir, 0, homedir, 0) == 0);
				if (!inhome && !str8_match(mnttype, str8_lit("9p"), 0))
				{
					fprintf(stderr, "9umount: %.*s: refusing to unmount non-9p fs\n", str8_varg(path));
				}
				else if (!inhome && !mountedby(scratch.arena, mntopts, user))
				{
					fprintf(stderr, "9umount: %.*s: not mounted by you\n", str8_varg(path));
				}
				else if (umount(mnt->mnt_dir))
				{
					fprintf(stderr, "9umount: umount %.*s: %s\n", str8_varg(mntdir), strerror(errno));
				}
				break;
			}
		}
		endmntent(fp);
		if (!ok)
		{
			fprintf(stderr, "9umount: %.*s not found in /proc/mounts\n", str8_varg(path));
		}
	}
	scratch_end(scratch);
}
