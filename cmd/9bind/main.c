#include <sys/mount.h>

// clang-format off
#include "base/inc.h"
#include "base/inc.c"
// clang-format on

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
	if (parsed.inputs.node_count != 2)
	{
		fprintf(stderr, "usage: 9bind old new\n");
		return 1;
	}
	String8 old = parsed.inputs.first->string;
	String8 new = parsed.inputs.first->next->string;
	struct stat st = {0};
	if (stat((char *)new.str, &st) || access((char *)new.str, W_OK))
	{
		fprintf(stderr, "9bind: %.*s: %s\n", str8_varg(new), strerror(errno));
		return 1;
	}
	if (st.st_mode & S_ISVTX)
	{
		fprintf(stderr, "9bind: refusing to bind over sticky directory %.*s\n", str8_varg(new));
		return 1;
	}
	if (mount((char *)old.str, (char *)new.str, NULL, MS_BIND, NULL))
	{
		fprintf(stderr, "9bind: bind failed: %s\n", strerror(errno));
		return 1;
	}
	arena_release(arena);
	return 0;
}
