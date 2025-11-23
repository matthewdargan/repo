#ifndef _9P_FS_H
#define _9P_FS_H

////////////////////////////////
//~ Includes

#include <grp.h>

////////////////////////////////
//~ Filesystem Types

typedef struct TempNode9P TempNode9P;
struct TempNode9P
{
	String8 path;
	String8 name;
	String8 content;
	String8 user_id;
	String8 group_id;
	Qid qid;
	u32 mode;
	u32 access_time;
	u32 modify_time;
	b32 is_directory;
	TempNode9P *first_child;
	TempNode9P *next_sibling;
	TempNode9P *parent;
};

typedef struct FsContext9P FsContext9P;
struct FsContext9P
{
	String8 root_path;
	String8 tmp_path;
	Arena *tmp_arena;
	TempNode9P *tmp_root;
	u64 tmp_qid_count;
	b32 readonly;
	u32 uid_offset;
	u32 gid_offset;
};

typedef struct FsHandle9P FsHandle9P;
struct FsHandle9P
{
	String8 path;
	int fd;
	void *dir_handle;
	u64 dir_position;
	b32 is_directory;
	TempNode9P *tmp_node;
	FsContext9P *ctx;
};

typedef struct PathResolution9P PathResolution9P;
struct PathResolution9P
{
	String8 absolute_path;
	b32 valid;
	String8 error;
};

typedef struct DirIterator9P DirIterator9P;
struct DirIterator9P
{
	void *dir_handle;
	u64 position;
	String8 path;
	String8 cached_entries;
	TempNode9P *tmp_node;
	TempNode9P *tmp_current;
};

typedef struct FidAuxiliary9P FidAuxiliary9P;
struct FidAuxiliary9P
{
	String8 path;
	FsHandle9P *handle;
	DirIterator9P *dir_iter;
	u32 open_mode;
	b32 is_tmp;
	void *tmp_data;
};

typedef u32 StorageBackend9P;
enum
{
	StorageBackend9P_Disk,
	StorageBackend9P_ArenaTemp,
};

////////////////////////////////
//~ Context Management

internal FsContext9P *fs9p_context_alloc(Arena *arena, String8 root_path, String8 tmp_path, b32 readonly);

////////////////////////////////
//~ Path Operations

internal String8 fs9p_path_join(Arena *arena, String8 base, String8 name);
internal String8 fs9p_basename(Arena *arena, String8 path);
internal String8 fs9p_dirname(Arena *arena, String8 path);
internal b32 fs9p_path_is_safe(String8 path);
internal PathResolution9P fs9p_resolve_path(Arena *arena, FsContext9P *ctx, String8 base_path, String8 name);
internal String8 os_path_from_fs9p_path(Arena *arena, FsContext9P *ctx, String8 relative_path);

////////////////////////////////
//~ File Operations

internal FsHandle9P *fs9p_open(Arena *arena, FsContext9P *ctx, String8 path, u32 mode);
internal void fs9p_close(FsHandle9P *handle);
internal String8 fs9p_read(Arena *arena, FsHandle9P *handle, u64 offset, u64 count);
internal u64 fs9p_write(FsHandle9P *handle, u64 offset, String8 data);
internal b32 fs9p_create(FsContext9P *ctx, String8 path, u32 permissions, u32 mode);
internal void fs9p_remove(FsContext9P *ctx, String8 path);

////////////////////////////////
//~ Metadata Operations

internal Dir9P fs9p_stat(Arena *arena, FsContext9P *ctx, String8 path);
internal b32 fs9p_wstat(FsContext9P *ctx, String8 path, Dir9P *dir);

////////////////////////////////
//~ Directory Operations

internal DirIterator9P *fs9p_opendir(Arena *arena, FsContext9P *ctx, String8 path);
internal String8 fs9p_readdir(Arena *arena, FsContext9P *ctx, DirIterator9P *iter, u64 offset, u64 count);
internal void fs9p_closedir(DirIterator9P *iter);

////////////////////////////////
//~ Temporary Storage Helpers

internal TempNode9P *temp9p_node_lookup(TempNode9P *root, String8 path);
internal TempNode9P *temp9p_node_create(Arena *arena, FsContext9P *ctx, String8 path, String8 name, b32 is_dir,
                                        u32 mode);

////////////////////////////////
//~ Temporary Storage Operations

internal TempNode9P *temp9p_open(FsContext9P *ctx, String8 path);
internal String8 temp9p_read(Arena *arena, TempNode9P *node, u64 offset, u64 count);
internal u64 temp9p_write(Arena *arena, TempNode9P *node, u64 offset, String8 data);
internal b32 temp9p_create(Arena *arena, FsContext9P *ctx, String8 path, u32 permissions);
internal void temp9p_remove(FsContext9P *ctx, String8 path);
internal Dir9P temp9p_stat(Arena *arena, TempNode9P *node);
internal void temp9p_wstat(Arena *arena, TempNode9P *node, Dir9P *dir);
internal String8 temp9p_readdir(Arena *arena, TempNode9P *node, TempNode9P **iter, u64 offset, u64 count);

////////////////////////////////
//~ Backend Routing

internal StorageBackend9P fs9p_get_backend(String8 path);

////////////////////////////////
//~ UID/GID Conversion

internal String8 str8_from_uid(Arena *arena, u32 uid);
internal String8 str8_from_gid(Arena *arena, u32 gid);

#endif // _9P_FS_H
