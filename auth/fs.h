#ifndef AUTH_FS_H
#define AUTH_FS_H

////////////////////////////////
//~ File Types

typedef enum
{
  Auth_File_None,
  Auth_File_Root,
  Auth_File_RPC,
  Auth_File_Ctl,
  Auth_File_Log,
} Auth_File_Type;

////////////////////////////////
//~ File Info

typedef struct Auth_File_Info Auth_File_Info;
struct Auth_File_Info
{
  Auth_File_Type type;
  String8 name;
  u64 qid_path;
  u32 qid_version;
  u32 mode;
  u64 size;
};

////////////////////////////////
//~ Filesystem State

typedef struct Auth_FS_State Auth_FS_State;
struct Auth_FS_State
{
  Arena *arena;
  Auth_RPC_State *rpc_state;
  String8 keys_path;
  Mutex mutex;
  String8List log_entries;
  u64 log_size;
};

////////////////////////////////
//~ Filesystem Operations

internal Auth_FS_State *auth_fs_alloc(Arena *arena, Auth_RPC_State *rpc_state, String8 keys_path);
internal void auth_fs_log(Auth_FS_State *fs, String8 entry);

internal Auth_File_Info auth_fs_lookup(Auth_FS_State *fs, String8 path);
internal Auth_File_Info auth_fs_stat_root(Auth_FS_State *fs);
internal String8List auth_fs_readdir(Arena *arena, Auth_File_Type type);

internal String8 auth_fs_read(Arena *arena, Auth_FS_State *fs, Auth_File_Type file_type, Auth_Conv *conv, u64 offset,
                              u64 count);
internal b32 auth_fs_write(Arena *arena, Auth_FS_State *fs, Auth_File_Type file_type, Auth_Conv **conv, String8 data);

#endif // AUTH_FS_H
