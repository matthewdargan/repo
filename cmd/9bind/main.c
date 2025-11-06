#include <sys/mount.h>

// clang-format off
#include "base/inc.h"
#include "base/inc.c"
// clang-format on

static void
entry_point(CmdLine *cmd_line)
{
	Temp scratch = scratch_begin(0, 0);
	Log *log     = log_alloc();
	log_select(log);
	log_scope_begin();

	if (cmd_line->inputs.node_count != 2)
	{
		log_error(str8_lit("usage: 9bind old new\n"));
	}
	else
	{
		String8 old    = cmd_line->inputs.first->string;
		String8 new    = cmd_line->inputs.first->next->string;
		struct stat st = {0};
		if (stat((char *)new.str, &st) || access((char *)new.str, W_OK))
		{
			log_errorf("9bind: %S: %s\n", new, strerror(errno));
		}
		else if (st.st_mode & S_ISVTX)
		{
			log_errorf("9bind: refusing to bind over sticky directory %S\n", new);
		}
		else if (mount((char *)old.str, (char *)new.str, 0, MS_BIND, 0))
		{
			log_errorf("9bind: bind failed: %s\n", strerror(errno));
		}
	}

	LogScopeResult result = log_scope_end(scratch.arena);
	if (result.strings[LogMsgKind_Error].size > 0)
	{
		fwrite(result.strings[LogMsgKind_Error].str, 1, result.strings[LogMsgKind_Error].size, stderr);
		fflush(stderr);
	}
	scratch_end(scratch);
}
