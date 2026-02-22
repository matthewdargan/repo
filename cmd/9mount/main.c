#define FUSE_USE_VERSION 35
#include <fuse3/fuse.h>

#include "base/inc.h"
#include "9p/inc.h"
#include "base/inc.c"
#include "9p/inc.c"

typedef enum ConnState ConnState;
enum ConnState
{
  ConnState_Disconnected,
  ConnState_Connected,
};

typedef struct MountState MountState;
struct MountState
{
  Arena *perm_arena;
  OS_Handle server_fd;
  String8 dial_str;
  String8 auth_daemon;
  String8 auth_id;
  String8 attach_path;
  b32 use_auth;
  u64 last_reconnect_time;
  u64 reconnect_backoff;
  u64 reconnections;
};

global MountState *g_mount;
global Mutex       g_reconnect_mutex;
global Mutex       g_client_rpc_mutex;
global u32         g_conn_state = ConnState_Disconnected;
global Client9P   *g_client;

////////////////////////////////
//~ Connection Recovery

internal void
reconnect_fail(void)
{
  ins_atomic_u32_eval_assign(&g_conn_state, ConnState_Disconnected);
  g_mount->reconnect_backoff = Min(g_mount->reconnect_backoff * 2, Million(60));
}

internal b32
reconnect_attempt_locked(Arena *arena)
{
  u32 state = ins_atomic_u32_eval(&g_conn_state);
  if(state == ConnState_Connected) { return 1; }

  u64 now     = os_now_microseconds();
  u64 jitter  = now % (g_mount->reconnect_backoff / 2 + 1);
  u64 backoff = g_mount->reconnect_backoff / 2 + jitter;
  if(now - g_mount->last_reconnect_time < backoff) { return 0; }

  g_mount->last_reconnect_time = now;

  if(g_mount->server_fd.u64[0] != 0)
  {
    os_file_close(g_mount->server_fd);
    g_mount->server_fd = os_handle_zero();
  }

  Dial9PAddress addr = dial9p_parse(arena, g_mount->dial_str, str8_lit("tcp"), str8_lit("9pfs"));
  if(addr.host.size == 0) { reconnect_fail(); return 0; }

  String8 protocol = addr.protocol == Dial9PProtocol_Unix ? str8_lit("unix") : str8_lit("tcp");
  OS_Handle handle = dial9p_connect(arena, g_mount->dial_str, protocol, str8_lit("9pfs"));
  if(os_handle_match(handle, os_handle_zero())) { reconnect_fail(); return 0; }

  Client9P *client = 0;
  MutexScope(g_client_rpc_mutex)
  {
    client = client9p_mount(g_mount->perm_arena, handle.u64[0], g_mount->auth_daemon, g_mount->auth_id, g_mount->attach_path, g_mount->use_auth);
  }
  if(client == 0) { os_file_close(handle); reconnect_fail(); return 0; }

  g_mount->server_fd          = handle;
  ins_atomic_ptr_eval_assign(&g_client, client);
  ins_atomic_u32_eval_assign(&g_conn_state, ConnState_Connected);
  g_mount->reconnections     += 1;
  g_mount->reconnect_backoff  = Million(1);
  return 1;
}

internal b32
reconnect(Arena *arena)
{
  if(ins_atomic_u32_eval(&g_conn_state) == ConnState_Connected) { return 1; }
  b32 result = 0;
  MutexScope(g_reconnect_mutex) { result = reconnect_attempt_locked(arena); }
  return result;
}

internal ClientFid9P *
walk_path(Arena *arena, String8 path)
{
  if(!reconnect(arena)) { return 0; }

  Client9P *client = ins_atomic_ptr_eval(&g_client);
  if(path.size == 0 || (path.size == 1 && path.str[0] == '/')) { return client->root; }

  String8 trimmed = path;
  if(trimmed.str[0] == '/') { trimmed = str8_skip(trimmed, 1); }

  ClientFid9P *result = 0;
  MutexScope(g_client_rpc_mutex) { result = client9p_fid_walk(arena, client->root, trimmed); }

  return result;
}

internal Dir9P
dir9p_wstat_mask(void)
{
  Dir9P dir       = {0};
  dir.server_type = max_u32;
  dir.server_dev  = max_u32;
  dir.mode        = max_u32;
  dir.access_time = max_u32;
  dir.modify_time = max_u32;
  dir.length      = max_u64;
  return dir;
}

////////////////////////////////
//~ FUSE Operations

static int
fs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
  Arena *arena = arena_alloc();
  int result = 0;

  ClientFid9P *fid = walk_path(arena, str8_cstring((char *)path));
  if(fid != 0)
  {
    Dir9P dir;
    MutexScope(g_client_rpc_mutex) { dir = client9p_fid_stat(arena, fid); }
    if(dir.name.size == 0)
    {
      ins_atomic_u32_eval_assign(&g_conn_state, ConnState_Disconnected);
      result = -EIO;
    }
    else
    {
      MemoryZeroStruct(stbuf);
      stbuf->st_ino     = fid->qid.path;
      stbuf->st_mode    = (dir.mode & 0777) | ((dir.mode & P9_ModeFlag_Directory) ? S_IFDIR : S_IFREG);
      stbuf->st_nlink   = 1;
      stbuf->st_uid     = getuid();
      stbuf->st_gid     = getgid();
      stbuf->st_size    = dir.length;
      stbuf->st_atime   = dir.access_time;
      stbuf->st_mtime   = dir.modify_time;
      stbuf->st_ctime   = dir.modify_time;
      stbuf->st_blksize = KB(4);
      stbuf->st_blocks  = (dir.length + 511) / 512;
    }
  }
  else { result = -ENOENT; }

  arena_release(arena);
  return result;
}

static int
fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags)
{
  Arena *arena = arena_alloc();
  int result   = 0;

  DirList9P list = {0};

  ClientFid9P *fid = (ClientFid9P *)fi->fh;
  if(fid != 0)
  {
    MutexScope(g_client_rpc_mutex) { list = client9p_fid_read_dirs(arena, fid); }
  }
  else { result = -EBADF; }

  if(result == 0)
  {
    filler(buf, ".", 0, 0, 0);
    filler(buf, "..", 0, 0, 0);

    for(DirNode9P *node = list.first; node != 0; node = node->next)
    {
      String8 name = str8_copy(arena, node->dir.name);
      filler(buf, (char *)name.str, 0, 0, 0);
    }
  }

  arena_release(arena);
  return result;
}

static int
fs_open(const char *path, struct fuse_file_info *fi)
{
  Arena *arena = arena_alloc();
  int result   = 0;

  u32 mode = open_mode_table[fi->flags & O_ACCMODE];
  if(fi->flags & O_TRUNC) { mode |= P9_OpenFlag_Truncate; }
  b32 remove_on_close = (mode & P9_OpenFlag_RemoveOnClose) != 0;
  mode               &= ~P9_OpenFlag_RemoveOnClose;

  ClientFid9P *fid = walk_path(arena, str8_cstring((char *)path));
  if(fid == 0) { result = -ENOENT; }
  else
  {
    ClientFid9P *open_fid = 0;
    b32 ok = 0;
    MutexScope(g_client_rpc_mutex)
    {
      open_fid = client9p_fid_walk(g_mount->perm_arena, fid, str8_zero());
      if(open_fid != 0) { ok = client9p_fid_open(arena, open_fid, mode); }
    }
    if(open_fid == 0) { result = -EIO; }
    else if(!ok)      { result = -EACCES; }
    else
    {
      fi->fh = (u64)open_fid | (remove_on_close ? 1 : 0);
      fi->direct_io = 1;
    }
  }

  arena_release(arena);
  return result;
}

static int
fs_release(const char *path, struct fuse_file_info *fi)
{
  if(!fi->fh) { return 0; }

  Arena *arena        = arena_alloc();
  b32 remove_on_close = fi->fh & 1;
  ClientFid9P *fid    = (ClientFid9P *)(fi->fh & ~1ULL);

  MutexScope(g_client_rpc_mutex)
  {
    if(remove_on_close) { client9p_fid_remove(arena, fid); }
    else                { client9p_fid_close(arena, fid); }
  }

  arena_release(arena);
  return 0;
}

static int
fs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
  Arena *arena = arena_alloc();
  int result   = 0;

  ClientFid9P *fid = (ClientFid9P *)(fi->fh & ~1ULL);
  if(fid == 0) { result = -EBADF; }
  else
  {
    s64 n;
    MutexScope(g_client_rpc_mutex) { n = client9p_fid_pread(arena, fid, buf, size, offset); }
    if(n < 0)
    {
      ins_atomic_u32_eval_assign(&g_conn_state, ConnState_Disconnected);
      result = -EIO;
    }
    else { result = (int)n; }
  }

  arena_release(arena);
  return result;
}

static int
fs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
  Arena *arena = arena_alloc();
  int result   = 0;

  ClientFid9P *fid = (ClientFid9P *)(fi->fh & ~1ULL);
  if(fid == 0) { result = -EBADF; }
  else
  {
    s64 n;
    MutexScope(g_client_rpc_mutex) { n = client9p_fid_pwrite(arena, fid, (void *)buf, size, offset); }
    if(n < 0)
    {
      ins_atomic_u32_eval_assign(&g_conn_state, ConnState_Disconnected);
      result = -EIO;
    }
    else { result = (int)n; }
  }

  arena_release(arena);
  return result;
}

static int
fs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
  Arena *arena = arena_alloc();
  int result   = 0;

  String8 path_str = str8_cstring((char *)path);
  u64 last_slash   = str8_find_needle_reverse(path_str, path_str.size, str8_lit("/"), 0);
  String8 dir_path = str8_prefix(path_str, last_slash);
  String8 name     = str8_skip(path_str, last_slash + 1);

  ClientFid9P *dir_fid = walk_path(arena, dir_path);
  if(dir_fid == 0) { result = -ENOENT; }
  else
  {
    u32 open_mode        = open_mode_table[fi->flags & O_ACCMODE];
    ClientFid9P *new_fid = 0;
    b32 ok               = 0;
    MutexScope(g_client_rpc_mutex)
    {
      new_fid = client9p_fid_walk(g_mount->perm_arena, dir_fid, str8_zero());
      if(new_fid != 0) { ok = client9p_fid_create(arena, new_fid, name, open_mode, mode); }
    }
    if(new_fid == 0) { result = -EIO; }
    else if(!ok)     { result = -EACCES; }
    else
    {
      fi->fh = (u64)new_fid;
      fi->direct_io = 1;
    }
  }

  arena_release(arena);
  return result;
}

static int
fs_mkdir(const char *path, mode_t mode)
{
  Arena *arena = arena_alloc();
  int result   = 0;

  String8 path_str = str8_cstring((char *)path);
  u64 last_slash   = str8_find_needle_reverse(path_str, path_str.size, str8_lit("/"), 0);
  String8 dir_path = str8_prefix(path_str, last_slash);
  String8 name     = str8_skip(path_str, last_slash + 1);

  ClientFid9P *dir_fid = walk_path(arena, dir_path);
  if(dir_fid == 0) { result = -ENOENT; }
  else
  {
    u32 dir_mode         = (mode & 0777) | P9_ModeFlag_Directory;
    ClientFid9P *new_fid = 0;
    b32 ok               = 0;
    MutexScope(g_client_rpc_mutex)
    {
      new_fid = client9p_fid_walk(arena, dir_fid, str8_zero());
      if(new_fid != 0)
      {
        ok = client9p_fid_create(arena, new_fid, name, P9_OpenFlag_Read, dir_mode);
        if(ok) { client9p_fid_close(arena, new_fid); }
      }
    }
    if(new_fid == 0) { result = -EIO; }
    else if(!ok)     { result = -EACCES; }
  }

  arena_release(arena);
  return result;
}

static int
fs_unlink(const char *path)
{
  Arena *arena = arena_alloc();
  int result   = 0;

  ClientFid9P *fid = walk_path(arena, str8_cstring((char *)path));
  if(fid == 0) { result = -ENOENT; }
  else
  {
    b32 ok;
    MutexScope(g_client_rpc_mutex) { ok = client9p_fid_remove(arena, fid); }
    result = ok ? 0 : -EIO;
  }

  arena_release(arena);
  return result;
}

static int fs_rmdir(const char *path) { return fs_unlink(path); }

static int
fs_rename(const char *from, const char *to, unsigned int flags)
{
  if(flags) { return -EINVAL; }

  Arena *arena = arena_alloc();
  int result   = 0;

  String8 from_str = str8_cstring((char *)from);
  String8 to_str   = str8_cstring((char *)to);

  u64 from_slash   = str8_find_needle_reverse(from_str, from_str.size, str8_lit("/"), 0);
  u64 to_slash     = str8_find_needle_reverse(to_str, to_str.size, str8_lit("/"), 0);
  String8 from_dir = str8_prefix(from_str, from_slash);
  String8 to_dir   = str8_prefix(to_str, to_slash);

  if(!str8_match(from_dir, to_dir, 0)) { result = -EXDEV; }
  else
  {
    ClientFid9P *fid = walk_path(arena, from_str);
    if(fid == 0) { result = -ENOENT; }
    else
    {
      String8 new_name = str8_skip(to_str, to_slash + 1);
      Dir9P dir        = dir9p_wstat_mask();
      dir.name         = new_name;

      ClientFid9P *dst_fid = walk_path(arena, to_str);
      if(dst_fid != 0)
      {
        b32 ok;
        MutexScope(g_client_rpc_mutex) { ok = client9p_fid_remove(arena, dst_fid); }
        if(!ok) { result = -EIO; }
      }

      if(result == 0)
      {
        b32 ok;
        MutexScope(g_client_rpc_mutex) { ok = client9p_fid_wstat(arena, fid, dir); }
        result = ok ? 0 : -EIO;
      }
    }
  }

  arena_release(arena);
  return result;
}

static int
fs_truncate(const char *path, off_t size, struct fuse_file_info *fi)
{
  Arena *arena = arena_alloc();
  int result   = 0;

  ClientFid9P *fid = walk_path(arena, str8_cstring((char *)path));
  if(fid == 0) { result = -ENOENT; }
  else
  {
    Dir9P dir  = dir9p_wstat_mask();
    dir.length = (u64)size;

    b32 ok;
    MutexScope(g_client_rpc_mutex) { ok = client9p_fid_wstat(arena, fid, dir); }

    result = ok ? 0 : -EIO;
  }

  arena_release(arena);
  return result;
}

static int
fs_chmod(const char *path, mode_t mode, struct fuse_file_info *fi)
{
  Arena *arena = arena_alloc();
  int result   = 0;

  ClientFid9P *fid = walk_path(arena, str8_cstring((char *)path));
  if(fid == 0)
  {
    result = -ENOENT;
  }
  else
  {
    Dir9P dir = dir9p_wstat_mask();
    dir.mode  = mode;

    b32 ok;
    MutexScope(g_client_rpc_mutex) { ok = client9p_fid_wstat(arena, fid, dir); }

    result = ok ? 0 : -EIO;
  }

  arena_release(arena);
  return result;
}

static int
fs_utimens(const char *path, const struct timespec tv[2],
           struct fuse_file_info *fi)
{
  Arena *arena = arena_alloc();
  int result   = 0;

  ClientFid9P *fid = walk_path(arena, str8_cstring((char *)path));
  if(fid == 0)
  {
    result = -ENOENT;
  }
  else
  {
    Dir9P dir       = dir9p_wstat_mask();
    dir.access_time = tv[0].tv_sec;
    dir.modify_time = tv[1].tv_sec;

    b32 ok;
    MutexScope(g_client_rpc_mutex) { ok = client9p_fid_wstat(arena, fid, dir); }

    result = ok ? 0 : -EIO;
  }

  arena_release(arena);
  return result;
}

static int fs_access(const char *path, int mask) { return 0; }

static int
fs_statfs(const char *path, struct statvfs *stbuf)
{
  MemoryZeroStruct(stbuf);
  stbuf->f_bsize   = KB(4);
  stbuf->f_frsize  = KB(4);
  stbuf->f_blocks  = Million(1);
  stbuf->f_bfree   = Thousand(500);
  stbuf->f_bavail  = Thousand(500);
  stbuf->f_files   = Million(1);
  stbuf->f_ffree   = Thousand(500);
  stbuf->f_namemax = 255;
  return 0;
}

static int
fs_opendir(const char *path, struct fuse_file_info *fi)
{
  Arena *arena = arena_alloc();
  int result   = 0;

  ClientFid9P *dir_fid = walk_path(arena, str8_cstring((char *)path));
  if(dir_fid == 0) { result = -ENOENT; }
  else
  {
    ClientFid9P *fid = 0;
    b32 ok           = 0;
    MutexScope(g_client_rpc_mutex)
    {
      fid = client9p_fid_walk(g_mount->perm_arena, dir_fid, str8_zero());
      if(fid != 0) { ok = client9p_fid_open(arena, fid, P9_OpenFlag_Read); }
    }
    if(fid == 0) { result = -EIO; }
    else if(!ok) { result = -EACCES; }
    else         { fi->fh = (u64)fid; }
  }

  arena_release(arena);
  return result;
}

static int
fs_releasedir(const char *path, struct fuse_file_info *fi)
{
  if(!fi->fh) { return 0; }

  Arena *arena = arena_alloc();

  ClientFid9P *fid = (ClientFid9P *)fi->fh;
  MutexScope(g_client_rpc_mutex) { client9p_fid_close(arena, fid); }

  arena_release(arena);
  return 0;
}

static const struct fuse_operations fs_ops = {
    .getattr    = fs_getattr,
    .opendir    = fs_opendir,
    .readdir    = fs_readdir,
    .releasedir = fs_releasedir,
    .open       = fs_open,
    .release    = fs_release,
    .read       = fs_read,
    .write      = fs_write,
    .create     = fs_create,
    .mkdir      = fs_mkdir,
    .unlink     = fs_unlink,
    .rmdir      = fs_rmdir,
    .rename     = fs_rename,
    .truncate   = fs_truncate,
    .chmod      = fs_chmod,
    .utimens    = fs_utimens,
    .access     = fs_access,
    .statfs     = fs_statfs,
};

////////////////////////////////
//~ Entry Point

internal void
entry_point(CmdLine *cmd_line)
{
  Arena *arena = arena_alloc();
  Log *log     = log_alloc();
  log_select(log);
  log_scope_begin();

  String8 auth_daemon = cmd_line_string(cmd_line, str8_lit("auth-daemon"));
  String8 auth_id     = cmd_line_string(cmd_line, str8_lit("auth-id"));
  String8 attach_path = cmd_line_string(cmd_line, str8_lit("aname"));

  if(auth_daemon.size == 0)  { auth_daemon = str8_lit("unix!/run/9auth/socket"); }
  if(attach_path.size == 0)  { attach_path = str8_lit("/"); }

  b32 use_auth = auth_id.size > 0;

  if(cmd_line->inputs.node_count != 2)
  {
    log_error(str8_lit("usage: 9mount [options] <dial> <mtpt>\n"
                       "options:\n"
                       "  --auth-daemon=<addr>    Auth daemon address (default: unix!/run/9auth/socket)\n"
                       "  --auth-id=<id>          Server identity for authentication (enables auth when present)\n"
                       "  --aname=<path>          Remote path to attach (default: /)\n"
                       "examples:\n"
                       "  9mount tcp!nas!5640 /mnt/media\n"
                       "  9mount --auth-id=nas tcp!nas!5640 /mnt/media\n"
                       "  9mount --auth-id=e2etest.local unix!/tmp/9pfs /mnt/test\n"
                       "  9mount --auth-id=e2etest.local --aname=subdir unix!/tmp/9pfs /mnt/test\n"));
    log_scope_flush(arena);
    return;
  }

  String8 dial        = cmd_line->inputs.first->string;
  String8 mount_point = cmd_line->inputs.first->next->string;

  Temp scratch = scratch_begin(&arena, 1);

  Dial9PAddress addr = dial9p_parse(scratch.arena, dial, str8_lit("tcp"), str8_lit("9pfs"));
  if(addr.host.size == 0)
  {
    log_errorf("9mount: invalid dial string %S\n", dial);
    log_scope_flush(scratch.arena);
    scratch_end(scratch);
    return;
  }

  String8 protocol_str = addr.protocol == Dial9PProtocol_Unix ? str8_lit("unix") : str8_lit("tcp");
  OS_Handle handle     = dial9p_connect(scratch.arena, dial, protocol_str, str8_lit("9pfs"));

  if(os_handle_match(handle, os_handle_zero()))
  {
    log_errorf("9mount: connection failed to %S\n", dial);
    log_scope_flush(scratch.arena);
    scratch_end(scratch);
    return;
  }

  Arena *mount_arena    = arena_alloc();
  String8 auth_id_param = use_auth ? auth_id : str8_zero();
  Client9P *client      = client9p_mount(mount_arena, handle.u64[0], auth_daemon, auth_id_param, attach_path, use_auth);

  if(client == 0)
  {
    log_errorf("9mount: mount failed for %S\n", dial);
    os_file_close(handle);
    log_scope_flush(scratch.arena);
    scratch_end(scratch);
    return;
  }

  g_mount                    = push_array(mount_arena, MountState, 1);
  g_mount->perm_arena        = mount_arena;
  g_mount->server_fd         = handle;
  g_mount->dial_str          = str8_copy(mount_arena, dial);
  g_mount->auth_daemon       = str8_copy(mount_arena, auth_daemon);
  g_mount->auth_id           = str8_copy(mount_arena, auth_id);
  g_mount->attach_path       = str8_copy(mount_arena, attach_path);
  g_mount->use_auth          = use_auth;
  g_mount->reconnect_backoff = Million(1);

  g_reconnect_mutex  = mutex_alloc();
  g_client_rpc_mutex = mutex_alloc();
  ins_atomic_u32_eval_assign(&g_conn_state, ConnState_Connected);
  ins_atomic_ptr_eval_assign(&g_client, client);

  log_infof("9mount: mounted %S at %S%s\n", dial, mount_point, use_auth ? " (authenticated)" : "");
  log_scope_flush(scratch.arena);

  struct fuse_args args = FUSE_ARGS_INIT(0, 0);
  fuse_opt_add_arg(&args, "9mount");
  fuse_opt_add_arg(&args, "-o");
  fuse_opt_add_arg(&args, "fsname=9p");

  String8 mount_point_copy = str8_copy(scratch.arena, mount_point);
  fuse_opt_add_arg(&args, (char *)mount_point_copy.str);

  int ret = fuse_main(args.argc, args.argv, &fs_ops, 0);

  fuse_opt_free_args(&args);

  log_infof("9mount: unmounting (reconnections=%llu)\n",
            g_mount->reconnections);

  client9p_unmount(mount_arena, client);

  scratch_end(scratch);

  if(ret != 0)
  {
    log_errorf("9mount: fuse_main failed with code %d\n", ret);
  }
}
