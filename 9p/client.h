#ifndef _9P_CLIENT_H
#define _9P_CLIENT_H

////////////////////////////////
//~ Includes

#include <pwd.h>

////////////////////////////////
//~ Client Constants

read_only global u32 open_mode_table[8] = {0,
                                           P9_OpenFlag_Execute,
                                           P9_OpenFlag_Write,
                                           P9_OpenFlag_ReadWrite,
                                           P9_OpenFlag_Read,
                                           P9_OpenFlag_Execute,
                                           P9_OpenFlag_ReadWrite,
                                           P9_OpenFlag_ReadWrite};

////////////////////////////////
//~ Client Types

typedef struct Client9P Client9P;
struct Client9P
{
  u64 fd;
  u32 max_message_size;
  u32 next_tag;
  u32 next_fid;
  struct ClientFid9P *root;
};

typedef struct ClientFid9P ClientFid9P;
struct ClientFid9P
{
  u32 fid;
  u32 mode;
  Qid qid;
  u64 offset;
  Client9P *client;
};

////////////////////////////////
//~ Client Connection

internal Client9P *client9p_init(Arena *arena, u64 fd);
internal ClientFid9P *client9p_auth(Arena *arena, Client9P *client, String8 user_name, String8 attach_path);
internal Client9P *client9p_mount(Arena *arena, u64 fd, String8 attach_path, b32 use_auth);
internal void client9p_unmount(Arena *arena, Client9P *client);
internal Message9P client9p_rpc(Arena *arena, Client9P *client, Message9P tx);
internal b32 client9p_version(Arena *arena, Client9P *client, u32 max_message_size);
internal ClientFid9P *client9p_tauth(Arena *arena, Client9P *client, String8 user_name, String8 attach_path);
internal ClientFid9P *client9p_attach(Arena *arena, Client9P *client, u32 auth_fid, String8 user_name,
                                      String8 attach_path);
internal ClientFid9P *client9p_create(Arena *arena, Client9P *client, String8 name, u32 mode, u32 permissions);
internal b32 client9p_remove(Arena *arena, Client9P *client, String8 name);
internal ClientFid9P *client9p_open(Arena *arena, Client9P *client, String8 name, u32 mode);
internal Dir9P client9p_stat(Arena *arena, Client9P *client, String8 name);
internal b32 client9p_wstat(Arena *arena, Client9P *client, String8 name, Dir9P dir);
internal b32 client9p_access(Arena *arena, Client9P *client, String8 name, u32 mode);

////////////////////////////////
//~ Fid Operations

internal void client9p_fid_close(Arena *arena, ClientFid9P *fid);
internal ClientFid9P *client9p_fid_walk(Arena *arena, ClientFid9P *fid, String8 path);
internal b32 client9p_fid_create(Arena *arena, ClientFid9P *fid, String8 name, u32 mode, u32 permissions);
internal b32 client9p_fid_remove(Arena *arena, ClientFid9P *fid);
internal b32 client9p_fid_open(Arena *arena, ClientFid9P *fid, u32 mode);
internal s64 client9p_fid_pread(Arena *arena, ClientFid9P *fid, void *buf, u64 n, s64 offset);
internal s64 client9p_fid_read_range(Arena *arena, ClientFid9P *fid, void *buf, Rng1U64 range);
internal s64 client9p_fid_pwrite(Arena *arena, ClientFid9P *fid, void *buf, u64 n, s64 offset);
internal s64 client9p_fid_write_range(Arena *arena, ClientFid9P *fid, void *buf, Rng1U64 range);
internal s64 client9p_fid_seek(Arena *arena, ClientFid9P *fid, s64 offset, u32 type);
internal DirList9P client9p_dir_list_from_str8(Arena *arena, String8 buffer);
internal DirList9P client9p_fid_read_dirs(Arena *arena, ClientFid9P *fid);
internal Dir9P client9p_fid_stat(Arena *arena, ClientFid9P *fid);
internal b32 client9p_fid_wstat(Arena *arena, ClientFid9P *fid, Dir9P dir);

#endif // _9P_CLIENT_H
