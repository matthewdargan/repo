#include <dirent.h>
#include <errno.h>
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

int
main(int argc, char *argv[])
{
	sysinfo = (Sysinfo){
	    .nprocs = sysconf(_SC_NPROCESSORS_ONLN),
	    .pagesz = sysconf(_SC_PAGESIZE),
	    .lpagesz = 0x200000,
	};
	Arenaparams ap = {
	    .flags = arenaflags,
	    .ressz = arenaressz,
	    .cmtsz = arenacmtsz,
	};
	Arena *arena = arenaalloc(ap);
	String8list args = osargs(arena, argc, argv);
	Cmd parsed = cmdparse(arena, args);
	if (parsed.inputs.nnode != 2)
	{
		fprintf(stderr, "usage: 9bind old new\n");
		return 1;
	}
	String8 old = parsed.inputs.start->str;
	String8 new = parsed.inputs.start->next->str;
	struct stat st = {0};
	if (stat((char *)new.str, &st) || access((char *)new.str, W_OK))
	{
		fprintf(stderr, "9bind: %.*s: %s\n", str8varg(new), strerror(errno));
		return 1;
	}
	if (st.st_mode & S_ISVTX)
	{
		fprintf(stderr, "9bind: refusing to bind over sticky directory %.*s\n", str8varg(new));
		return 1;
	}
	if (mount((char *)old.str, (char *)new.str, NULL, MS_BIND, NULL))
	{
		fprintf(stderr, "9bind: bind failed: %s\n", strerror(errno));
		return 1;
	}
	arenarelease(arena);
	return 0;
}
