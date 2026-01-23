#include <sys/mount.h>

#include "base/inc.h"
#include "base/inc.c"

////////////////////////////////
//~ Entry Point

internal void
entry_point(CmdLine *cmd_line)
{
  Temp scratch = scratch_begin(0, 0);
  Log *log = log_alloc();
  log_select(log);
  log_scope_begin();

  if(cmd_line->inputs.node_count != 2)
  {
    log_error(str8_lit("usage: 9bind <old> <new>\n"));
  }
  else
  {
    String8 old = cmd_line->inputs.first->string;
    String8 new = cmd_line->inputs.first->next->string;
    String8 old_copy = str8_copy(scratch.arena, old);
    String8 new_copy = str8_copy(scratch.arena, new);
    struct stat st = {0};
    if(stat((char *)new_copy.str, &st) || access((char *)new_copy.str, W_OK))
    {
      log_errorf("9bind: %S: %s\n", new, strerror(errno));
    }
    else if(st.st_mode & S_ISVTX)
    {
      log_errorf("9bind: refusing to bind over sticky directory %S\n", new);
    }
    else if(mount((char *)old_copy.str, (char *)new_copy.str, 0, MS_BIND, 0))
    {
      log_errorf("9bind: bind failed: %s\n", strerror(errno));
    }
  }

  log_scope_flush(scratch.arena);
  scratch_end(scratch);
}
