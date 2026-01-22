#include <sys/mount.h>

// clang-format off
#include "base/inc.h"
#include "9p/inc.h"
#include "base/inc.c"
#include "9p/inc.c"
// clang-format on

////////////////////////////////
//~ Helper Functions

internal String8
resolve_host(Arena *arena, String8 host)
{
  String8 host_copy = str8_copy(arena, host);
  struct addrinfo *addr_info = 0;
  int getaddr_result = getaddrinfo((char *)host_copy.str, 0, 0, &addr_info);
  if(getaddr_result != 0)
  {
    return str8_zero();
  }
  char ip_buffer[INET6_ADDRSTRLEN] = {0};
  int getname_result =
      getnameinfo(addr_info->ai_addr, addr_info->ai_addrlen, ip_buffer, sizeof(ip_buffer), 0, 0, NI_NUMERICHOST);
  if(getname_result != 0)
  {
    freeaddrinfo(addr_info);
    return str8_zero();
  }
  String8 result = str8_copy(arena, str8_cstring(ip_buffer));
  freeaddrinfo(addr_info);
  return result;
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

  if(cmd_line->argc < 3)
  {
    log_error(str8_lit("usage: mount.9p <device> <mountpoint> [-o <options>]\n"
                       "This program is a mount(8) helper for 9P filesystems.\n"
                       "It is normally called by mount(8), not directly by users.\n"
                       "options:\n"
                       "  port=<port>             TCP port (default: 564)\n"
                       "  aname=<path>            Remote path to attach\n"
                       "  msize=<bytes>           Maximum 9P message size (default: 1048576)\n"));
  }
  else
  {
    String8 device = str8_cstring(cmd_line->argv[1]);
    String8 mountpoint = str8_cstring(cmd_line->argv[2]);
    String8 mountpoint_copy = str8_copy(scratch.arena, mountpoint);

    u64 port = 564;
    String8 aname = str8_zero();
    u64 msize = MB(1);
    String8 options_string = str8_zero();

    for(u64 i = 3; i < cmd_line->argc - 1; i += 1)
    {
      if(str8_match(str8_cstring(cmd_line->argv[i]), str8_lit("-o"), 0))
      {
        options_string = str8_cstring(cmd_line->argv[i + 1]);
        break;
      }
    }

    if(options_string.size > 0)
    {
      String8List option_list = str8_split(scratch.arena, options_string, (u8 *)",", 1, 0);
      for(String8Node *node = option_list.first; node != 0; node = node->next)
      {
        String8 option = node->string;
        u64 equals_pos = str8_find_needle(option, 0, str8_lit("="), 0);
        if(equals_pos < option.size)
        {
          String8 key = str8_prefix(option, equals_pos);
          String8 value = str8_skip(option, equals_pos + 1);
          if(str8_match(key, str8_lit("port"), 0))
          {
            port = u64_from_str8(value, 10);
          }
          else if(str8_match(key, str8_lit("aname"), 0))
          {
            aname = value;
          }
          else if(str8_match(key, str8_lit("msize"), 0))
          {
            msize = u64_from_str8(value, 10);
          }
        }
      }
    }

    uid_t uid = getuid();
    gid_t gid = getgid();
    struct passwd *passwd_entry = getpwuid(uid);
    if(passwd_entry == 0)
    {
      log_errorf("mount.9p: unknown uid %d\n", uid);
    }
    else
    {
      struct stat stat_result = {0};
      if(stat((char *)mountpoint_copy.str, &stat_result) || access((char *)mountpoint_copy.str, W_OK))
      {
        log_errorf("mount.9p: %S: %s\n", mountpoint, strerror(errno));
      }
      else if(stat_result.st_mode & S_ISVTX)
      {
        log_errorf("mount.9p: refusing to mount over sticky directory %S\n", mountpoint);
      }
      else
      {
        String8 device_address = resolve_host(scratch.arena, device);
        if(device_address.size == 0)
        {
          log_errorf("mount.9p: failed to resolve host %S\n", device);
        }
        else if(port == 0)
        {
          log_error(str8_lit("mount.9p: invalid port\n"));
        }
        else
        {
          String8List mount_options = {0};
          str8_list_push(scratch.arena, &mount_options, str8f(scratch.arena, "trans=tcp,port=%llu", port));
          str8_list_push(scratch.arena, &mount_options, str8_lit("noextend"));
          String8 user = str8_cstring(passwd_entry->pw_name);
          str8_list_push(scratch.arena, &mount_options, str8f(scratch.arena, "uname=%S", user));
          if(aname.size > 0)
          {
            str8_list_push(scratch.arena, &mount_options, str8f(scratch.arena, "aname=%S", aname));
          }
          if(msize > 0)
          {
            str8_list_push(scratch.arena, &mount_options, str8f(scratch.arena, "msize=%llu", msize));
          }
          str8_list_push(scratch.arena, &mount_options, str8f(scratch.arena, "dfltuid=%d,dfltgid=%d", uid, gid));

          StringJoin join = {0};
          join.sep = str8_lit(",");
          String8 option_string = str8_list_join(scratch.arena, mount_options, &join);
          String8 device_address_copy = str8_copy(scratch.arena, device_address);

          if(mount((char *)device_address_copy.str, (char *)mountpoint_copy.str, "9p", 0, (char *)option_string.str))
          {
            log_errorf("mount.9p: mount failed: %s\n", strerror(errno));
          }
        }
      }
    }
  }

  log_scope_flush(scratch.arena);
  scratch_end(scratch);
}
