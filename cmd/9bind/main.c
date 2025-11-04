#include <sys/mount.h>

// clang-format off
#include "base/inc.h"
#include "base/inc.c"
// clang-format on

static void
entry_point(CmdLine *cmd_line)
{
	Temp scratch = scratch_begin(NULL, 0);
	if (cmd_line->inputs.node_count != 2)
	{
		fprintf(stderr, "usage: 9bind old new\n");
		return;
	}
	String8 old    = cmd_line->inputs.first->string;
	String8 new    = cmd_line->inputs.first->next->string;
	struct stat st = {0};
	if (stat((char *)new.str, &st) || access((char *)new.str, W_OK))
	{
		fprintf(stderr, "9bind: %.*s: %s\n", str8_varg(new), strerror(errno));
		return;
	}
	if (st.st_mode & S_ISVTX)
	{
		fprintf(stderr, "9bind: refusing to bind over sticky directory %.*s\n", str8_varg(new));
		return;
	}
	if (mount((char *)old.str, (char *)new.str, NULL, MS_BIND, NULL))
	{
		fprintf(stderr, "9bind: bind failed: %s\n", strerror(errno));
		return;
	}
	scratch_end(scratch);
}
