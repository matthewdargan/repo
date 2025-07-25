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
	String8list opts;
	String8node *node;
	String8 opt, uname;

	opts = str8split(a, mntopts, (u8 *)",", 1, 0);
	for (node = opts.start; node; node = node->next) {
		opt = node->str;
		if (str8index(opt, 0, str8lit("uname="), 0) == 0) {
			uname = str8skip(opt, 6);
			return str8cmp(uname, user, 0);
		}
	}
	return 0;
}

int
main(int argc, char *argv[])
{
	Arenaparams ap;
	String8list args;
	Cmd parsed;
	uid_t uid;
	struct passwd *pw;
	int ret;
	String8node *node;
	String8 mtpt, path, mntdir, mnttype, mntopts, homedir, user;
	FILE *fp;
	struct mntent *mnt;
	b32 ok, inhome;

	sysinfo.nprocs = sysconf(_SC_NPROCESSORS_ONLN);
	sysinfo.pagesz = sysconf(_SC_PAGESIZE);
	sysinfo.lpagesz = 0x200000;
	ap.flags = arenaflags;
	ap.ressz = arenaressz;
	ap.cmtsz = arenacmtsz;
	arena = arenaalloc(ap);
	args = osargs(arena, argc, argv);
	parsed = cmdparse(arena, args);
	if (parsed.inputs.nnode == 0) {
		fprintf(stderr, "usage: 9umount mtpt...\n");
		return 1;
	}
	uid = getuid();
	pw = getpwuid(uid);
	if (pw == NULL) {
		fprintf(stderr, "9umount: unknown uid %d\n", uid);
		return 1;
	}
	ret = 0;
	for (node = parsed.inputs.start; node != NULL; node = node->next) {
		mtpt = node->str;
		path = abspath(mtpt);
		if (path.len == 0) {
			fprintf(stderr, "9umount: %.*s: %s\n", (int)mtpt.len, mtpt.str, strerror(errno));
			ret = 1;
			continue;
		}
		fp = setmntent("/proc/mounts", "r");
		if (fp == NULL) {
			fprintf(stderr, "9umount: could not open /proc/mounts: %s\n", strerror(errno));
			ret = 1;
			break;
		}
		ok = 0;
		for (;;) {
			mnt = getmntent(fp);
			if (mnt == NULL)
				break;
			mntdir = str8cstr(mnt->mnt_dir);
			if (str8cmp(path, mntdir, 0)) {
				ok = 1;
				mnttype = str8cstr(mnt->mnt_type);
				mntopts = str8cstr(mnt->mnt_opts);
				homedir = str8cstr(pw->pw_dir);
				user = str8cstr(pw->pw_name);
				inhome = (str8index(mntdir, 0, homedir, 0) == 0);
				if (!inhome && !str8cmp(mnttype, str8lit("9p"), 0)) {
					fprintf(stderr, "9umount: %.*s: refusing to unmount non-9p fs\n", (int)path.len, path.str);
					ret = 1;
				} else if (!inhome && !mountedby(arena, mntopts, user)) {
					fprintf(stderr, "9umount: %.*s: not mounted by you\n", (int)path.len, path.str);
					ret = 1;
				} else if (umount(mnt->mnt_dir)) {
					fprintf(stderr, "9umount: umount %.*s: %s\n", (int)mntdir.len, mntdir.str, strerror(errno));
					ret = 1;
				}
				break;
			}
		}
		endmntent(fp);
		if (!ok) {
			fprintf(stderr, "9umount: %.*s not found in /proc/mounts\n", (int)path.len, path.str);
			ret = 1;
		}
	}
	arenarelease(arena);
	return ret;
}
