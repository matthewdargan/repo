#include <dirent.h>
#include <errno.h>
#include <mntent.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

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

static b32
mountedby(Arena *a, String8 mntopts, String8 user)
{
	String8list opts = str8split(a, mntopts, (u8 *)",", 1, 0);
	for (String8node *node = opts.start; node != NULL; node = node->next)
	{
		String8 opt = node->str;
		if (str8index(opt, 0, str8lit("uname="), 0) == 0)
		{
			String8 uname = str8skip(opt, 6);
			return str8cmp(uname, user, 0);
		}
	}
	return 0;
}

int
main(int argc, char *argv[])
{
	sysinfo = (Sysinfo){.nprocs = sysconf(_SC_NPROCESSORS_ONLN), .pagesz = sysconf(_SC_PAGESIZE), .lpagesz = 0x200000};
	Arenaparams ap = {.flags = arenaflags, .ressz = arenaressz, .cmtsz = arenacmtsz};
	Arena *arena = arenaalloc(ap);
	String8list args = osargs(arena, argc, argv);
	Cmd parsed = cmdparse(arena, args);
	if (parsed.inputs.nnode == 0)
	{
		fprintf(stderr, "usage: 9umount mtpt...\n");
		return 1;
	}
	uid_t uid = getuid();
	struct passwd *pw = getpwuid(uid);
	if (pw == NULL)
	{
		fprintf(stderr, "9umount: unknown uid %d\n", uid);
		return 1;
	}
	int ret = 0;
	for (String8node *node = parsed.inputs.start; node != NULL; node = node->next)
	{
		String8 mtpt = node->str;
		String8 path = abspath(arena, mtpt);
		if (path.len == 0)
		{
			fprintf(stderr, "9umount: %.*s: %s\n", str8varg(mtpt), strerror(errno));
			ret = 1;
			continue;
		}
		FILE *fp = setmntent("/proc/mounts", "r");
		if (fp == NULL)
		{
			fprintf(stderr, "9umount: could not open /proc/mounts: %s\n", strerror(errno));
			ret = 1;
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
			String8 mntdir = str8cstr(mnt->mnt_dir);
			if (str8cmp(path, mntdir, 0))
			{
				ok = 1;
				String8 mnttype = str8cstr(mnt->mnt_type);
				String8 mntopts = str8cstr(mnt->mnt_opts);
				String8 homedir = str8cstr(pw->pw_dir);
				String8 user = str8cstr(pw->pw_name);
				b32 inhome = (str8index(mntdir, 0, homedir, 0) == 0);
				if (!inhome && !str8cmp(mnttype, str8lit("9p"), 0))
				{
					fprintf(stderr, "9umount: %.*s: refusing to unmount non-9p fs\n", str8varg(path));
					ret = 1;
				}
				else if (!inhome && !mountedby(arena, mntopts, user))
				{
					fprintf(stderr, "9umount: %.*s: not mounted by you\n", str8varg(path));
					ret = 1;
				}
				else if (umount(mnt->mnt_dir))
				{
					fprintf(stderr, "9umount: umount %.*s: %s\n", str8varg(mntdir), strerror(errno));
					ret = 1;
				}
				break;
			}
		}
		endmntent(fp);
		if (!ok)
		{
			fprintf(stderr, "9umount: %.*s not found in /proc/mounts\n", str8varg(path));
			ret = 1;
		}
	}
	arenarelease(arena);
	return ret;
}
