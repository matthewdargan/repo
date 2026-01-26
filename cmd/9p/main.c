#include "base/inc.h"
#include "9p/inc.h"
#include "base/inc.c"
#include "9p/inc.c"

////////////////////////////////
//~ Helper Functions

internal void
usage(void)
{
  log_error(
      str8_lit("usage: 9p [-A=<aname>] <address> <cmd> <args>\n"
               "cmds: create <name>..., read <name>, write <name>, remove <name>..., stat <name>, ls <name>...\n"));
}

internal Client9P *
client9p_connect(Arena *arena, String8 address, String8 aname)
{
  if(address.size == 0)
  {
    log_error(str8_lit("9p: namespace mounting not implemented\n"));
    return 0;
  }

  OS_Handle socket = dial9p_connect(arena, address, str8_lit("tcp"), str8_lit("9pfs"));
  if(os_handle_match(socket, os_handle_zero()))
  {
    log_errorf("9p: dial failed for '%S'\n", address);
    return 0;
  }

  u64 fd = socket.u64[0];
  Client9P *client = client9p_mount(arena, fd, str8_zero(), aname, 0);
  if(client == 0)
  {
    os_file_close(socket);
    log_error(str8_lit("9p: mount failed\n"));
    return 0;
  }

  return client;
}

internal void
ls_print_name(Arena *arena, Client9P *client, String8 name)
{
  Dir9P d = client9p_stat(arena, client, name);
  if(d.name.size == 0)
  {
    log_errorf("9p: failed to stat '%S'\n", name);
  }
  else if(d.mode & P9_ModeFlag_Directory)
  {
    ClientFid9P *fid = client9p_open(arena, client, name, P9_OpenFlag_Read);
    if(fid == 0)
    {
      log_errorf("9p: failed to open directory '%S'\n", name);
    }
    else
    {
      DirList9P list = client9p_fid_read_dirs(arena, fid);
      for(DirNode9P *node = list.first; node != 0; node = node->next)
      {
        log_infof("%S\n", node->dir.name);
      }
      client9p_fid_close(arena, fid);
    }
  }
  else
  {
    log_infof("%S\n", d.name);
  }
}

////////////////////////////////
//~ Entry Point

internal void
entry_point(CmdLine *cmd_line)
{
  Temp scratch = scratch_begin(0, 0);
  Log *log = log_alloc();
  log_select(log);
  log_scope_begin();
  String8 aname = str8_zero();

  if(cmd_line_has_argument(cmd_line, str8_lit("A")))
  {
    aname = cmd_line_string(cmd_line, str8_lit("A"));
  }
  if(cmd_line->inputs.node_count < 2)
  {
    usage();
  }
  else
  {
    String8Node *inputs = cmd_line->inputs.first;
    String8 address = inputs->string;
    String8 command = inputs->next->string;
    String8Node *args = inputs->next->next;

    // create name...
    if(str8_match(command, str8_lit("create"), 0))
    {
      Client9P *client = client9p_connect(scratch.arena, address, aname);
      if(client != 0)
      {
        for(String8Node *node = args; node != 0; node = node->next)
        {
          String8 name = node->string;
          ClientFid9P *fid = client9p_create(scratch.arena, client, name, P9_OpenFlag_Read, 0666);
          if(fid == 0)
          {
            log_errorf("9p: failed to create '%S'\n", name);
          }
          else
          {
            client9p_fid_close(scratch.arena, fid);
          }
        }
        client9p_unmount(scratch.arena, client);
      }
    }
    // read name
    else if(str8_match(command, str8_lit("read"), 0))
    {
      String8 name = args->string;
      Client9P *client = client9p_connect(scratch.arena, address, aname);
      if(client != 0)
      {
        ClientFid9P *fid = client9p_open(scratch.arena, client, name, P9_OpenFlag_Read);
        if(fid == 0)
        {
          log_errorf("9p: failed to open '%S'\n", name);
        }
        else
        {
          u8 *buf = push_array_no_zero(scratch.arena, u8, P9_DIR_ENTRY_MAX);
          for(;;)
          {
            s64 n = client9p_fid_pread(scratch.arena, fid, buf, P9_DIR_ENTRY_MAX, -1);
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
          client9p_fid_close(scratch.arena, fid);
        }
        client9p_unmount(scratch.arena, client);
      }
    }
    // write name
    else if(str8_match(command, str8_lit("write"), 0))
    {
      String8 name = args->string;
      Client9P *client = client9p_connect(scratch.arena, address, aname);
      if(client != 0)
      {
        ClientFid9P *fid = client9p_open(scratch.arena, client, name, P9_OpenFlag_Write | P9_OpenFlag_Truncate);
        if(fid == 0)
        {
          log_errorf("9p: failed to open '%S'\n", name);
        }
        else
        {
          u8 *buf = push_array_no_zero(scratch.arena, u8, P9_DIR_ENTRY_MAX);
          for(;;)
          {
            s64 n = read(STDIN_FILENO, buf, P9_DIR_ENTRY_MAX);
            if(n <= 0)
            {
              if(n < 0)
              {
                log_errorf("9p: write error: %s\n", strerror(errno));
              }
              break;
            }
            s64 nwrite = client9p_fid_pwrite(scratch.arena, fid, buf, n, -1);
            if(nwrite != n)
            {
              log_error(str8_lit("9p: write error\n"));
              break;
            }
          }
          client9p_fid_close(scratch.arena, fid);
        }
        client9p_unmount(scratch.arena, client);
      }
    }
    // remove name...
    else if(str8_match(command, str8_lit("remove"), 0))
    {
      Client9P *client = client9p_connect(scratch.arena, address, aname);
      if(client != 0)
      {
        for(String8Node *node = args; node != 0; node = node->next)
        {
          String8 name = node->string;
          if(client9p_remove(scratch.arena, client, name) < 0)
          {
            log_errorf("9p: failed to remove '%S'\n", name);
          }
        }
        client9p_unmount(scratch.arena, client);
      }
    }
    // stat name
    else if(str8_match(command, str8_lit("stat"), 0))
    {
      String8 name = args->string;
      Client9P *client = client9p_connect(scratch.arena, address, aname);
      if(client != 0)
      {
        Dir9P d = client9p_stat(scratch.arena, client, name);
        if(d.name.size == 0)
        {
          log_errorf("9p: failed to stat '%S'\n", name);
        }
        else
        {
          log_infof("%S %llu %u %S %S\n", d.name, d.length, d.modify_time, d.user_id, d.group_id);
        }
        client9p_unmount(scratch.arena, client);
      }
    }
    // ls name...
    else if(str8_match(command, str8_lit("ls"), 0))
    {
      Client9P *client = client9p_connect(scratch.arena, address, aname);
      if(client != 0)
      {
        String8Node *name_node = args;
        if(name_node == 0)
        {
          ls_print_name(scratch.arena, client, str8_lit("."));
        }
        else
        {
          for(; name_node != 0; name_node = name_node->next)
          {
            ls_print_name(scratch.arena, client, name_node->string);
          }
        }
        client9p_unmount(scratch.arena, client);
      }
    }
    else
    {
      log_errorf("9p: unsupported command '%S'\n", command);
    }
  }

  log_scope_flush(scratch.arena);
  scratch_end(scratch);
}
