////////////////////////////////
//~ Context Management

internal FsContext9P *
fs9p_context_alloc(Arena *arena, String8 root_path, String8 tmp_path, b32 readonly)
{
	FsContext9P *ctx = push_array(arena, FsContext9P, 1);
	ctx->root_path = str8_copy(arena, root_path);
	ctx->tmp_path = str8_copy(arena, tmp_path);
	ctx->tmp_arena = arena_alloc();
	ctx->readonly = readonly;
	ctx->tmp_qid_count = 1;
	ctx->tmp_root = temp9p_node_create(ctx->tmp_arena, ctx, str8_lit("tmp"), str8_lit("tmp"), 1, 0755);
	return ctx;
}

////////////////////////////////
//~ Path Operations

internal String8
fs9p_path_join(Arena *arena, String8 base, String8 name)
{
	if(base.size == 0)
	{
		return str8_copy(arena, name);
	}
	if(name.size == 0)
	{
		return str8_copy(arena, base);
	}

	b32 base_has_slash = (base.str[base.size - 1] == '/');
	b32 name_has_slash = (name.str[0] == '/');

	if(base_has_slash && name_has_slash)
	{
		u8 *buffer = push_array(arena, u8, base.size + name.size - 1);
		MemoryCopy(buffer, base.str, base.size);
		MemoryCopy(buffer + base.size, name.str + 1, name.size - 1);
		return str8(buffer, base.size + name.size - 1);
	}
	else if(base_has_slash || name_has_slash)
	{
		u8 *buffer = push_array(arena, u8, base.size + name.size);
		MemoryCopy(buffer, base.str, base.size);
		MemoryCopy(buffer + base.size, name.str, name.size);
		return str8(buffer, base.size + name.size);
	}
	else
	{
		u8 *buffer = push_array(arena, u8, base.size + 1 + name.size);
		MemoryCopy(buffer, base.str, base.size);
		buffer[base.size] = '/';
		MemoryCopy(buffer + base.size + 1, name.str, name.size);
		return str8(buffer, base.size + 1 + name.size);
	}
}

internal String8
fs9p_basename(Arena *arena, String8 path)
{
	if(path.size == 0)
	{
		return str8_lit(".");
	}

	// Strip trailing slashes
	u64 end = path.size;
	for(; end > 0 && path.str[end - 1] == '/'; end -= 1)
	{
	}

	if(end == 0)
	{
		return str8_lit("/");
	}

	// Find the last slash before the basename
	u64 last_slash = end;
	for(u64 i = end; i > 0; i -= 1)
	{
		if(path.str[i - 1] == '/')
		{
			last_slash = i - 1;
			break;
		}
	}

	if(last_slash == end)
	{
		// No slash found, entire path is basename
		String8 base = str8(path.str, end);
		return str8_copy(arena, base);
	}
	else
	{
		String8 base = str8(path.str + last_slash + 1, end - last_slash - 1);
		return str8_copy(arena, base);
	}
}

internal String8
fs9p_dirname(Arena *arena, String8 path)
{
	if(path.size == 0)
	{
		return str8_lit(".");
	}

	u64 last_slash = path.size;
	for(u64 i = path.size; i > 0; i -= 1)
	{
		if(path.str[i - 1] == '/')
		{
			last_slash = i - 1;
			break;
		}
	}

	if(last_slash == path.size)
	{
		return str8_lit(".");
	}
	else if(last_slash == 0)
	{
		return str8_lit("/");
	}
	else
	{
		String8 dir = str8(path.str, last_slash);
		return str8_copy(arena, dir);
	}
}

internal b32
fs9p_path_is_safe(String8 path)
{
	if(str8_match(path, str8_lit(".."), 0))
	{
		return 0;
	}

	for(u64 i = 0; i + 1 < path.size; i += 1)
	{
		if(path.str[i] == '.' && path.str[i + 1] == '.')
		{
			b32 is_start = (i == 0 || path.str[i - 1] == '/');
			b32 is_end = (i + 2 >= path.size || path.str[i + 2] == '/');
			if(is_start && is_end)
			{
				return 0;
			}
		}
	}

	if(path.size > 0 && path.str[0] == '/')
	{
		return 0;
	}

	return 1;
}

internal b32
fs9p_path_is_under_root(Arena *arena, FsContext9P *ctx, String8 canonical_path)
{
	String8 root_canonical = os_full_path_from_path(arena, ctx->root_path);
	if(root_canonical.size == 0)
	{
		return 0;
	}

	if(root_canonical.size > canonical_path.size)
	{
		return 0;
	}

	String8 path_prefix = str8_prefix(canonical_path, root_canonical.size);
	if(!str8_match(path_prefix, root_canonical, 0))
	{
		return 0;
	}

	if(root_canonical.size == canonical_path.size)
	{
		return 1;
	}

	if(canonical_path.str[root_canonical.size] != '/')
	{
		return 0;
	}

	return 1;
}

internal PathResolution9P
fs9p_resolve_path(Arena *arena, FsContext9P *ctx, String8 base_path, String8 name)
{
	PathResolution9P result = {0};

	if(!fs9p_path_is_safe(name))
	{
		result.valid = 0;
		result.error = str8_lit("unsafe path");
		return result;
	}

	String8 joined = fs9p_path_join(arena, base_path, name);
	String8 os_path = os_path_from_fs9p_path(arena, ctx, joined);
	String8 canonical = os_full_path_from_path(arena, os_path);
	// realpath() fails for non-existent files, but os_full_path_from_path returns the original path
	b32 canonicalized = (canonical.size > 0 && str8_match(canonical, os_path, 0) == 0);

	if(!canonicalized)
	{
		String8 root_canonical = os_full_path_from_path(arena, ctx->root_path);
		if(root_canonical.size > 0)
		{
			if(base_path.size == 0)
			{
				if(!fs9p_path_is_safe(name))
				{
					result.valid = 0;
					result.error = str8_lit("unsafe path");
					return result;
				}
				result.absolute_path = str8_copy(arena, name);
				result.valid = 1;
				return result;
			}

			String8 parent_path = fs9p_dirname(arena, joined);
			String8 parent_os_path = os_path_from_fs9p_path(arena, ctx, parent_path);
			String8 parent_canonical = os_full_path_from_path(arena, parent_os_path);

			if(parent_canonical.size > 0)
			{
				if(!fs9p_path_is_under_root(arena, ctx, parent_canonical))
				{
					result.valid = 0;
					result.error = str8_lit("path escapes root");
					return result;
				}
				String8 expected_canonical = fs9p_path_join(arena, parent_canonical, name);
				if(root_canonical.size > expected_canonical.size)
				{
					result.valid = 0;
					result.error = str8_lit("path escapes root");
					return result;
				}
				String8 expected_prefix = str8_prefix(expected_canonical, root_canonical.size);
				if(!str8_match(expected_prefix, root_canonical, 0))
				{
					result.valid = 0;
					result.error = str8_lit("path escapes root");
					return result;
				}
				if(expected_canonical.size > root_canonical.size && expected_canonical.str[root_canonical.size] != '/')
				{
					result.valid = 0;
					result.error = str8_lit("path escapes root");
					return result;
				}
			}
			else
			{
				if(joined.size > 0 && joined.str[0] == '/')
				{
					result.valid = 0;
					result.error = str8_lit("path escapes root");
					return result;
				}
			}
		}
		result.absolute_path = joined;
		result.valid = 1;
		return result;
	}

	if(!fs9p_path_is_under_root(arena, ctx, canonical))
	{
		// If canonical equals original, realpath() likely failed (file doesn't exist)
		// In this case, use fallback logic to validate via parent directory
		if(str8_match(canonical, os_path, 0))
		{
			// Paths match, so realpath() failed - use fallback validation
			String8 root_canonical = os_full_path_from_path(arena, ctx->root_path);
			if(root_canonical.size > 0)
			{
				if(base_path.size == 0)
				{
					if(!fs9p_path_is_safe(name))
					{
						result.valid = 0;
						result.error = str8_lit("unsafe path");
						return result;
					}
					result.absolute_path = str8_copy(arena, name);
					result.valid = 1;
					return result;
				}

				String8 parent_path = fs9p_dirname(arena, joined);
				String8 parent_os_path = os_path_from_fs9p_path(arena, ctx, parent_path);
				String8 parent_canonical = os_full_path_from_path(arena, parent_os_path);

				if(parent_canonical.size > 0)
				{
					if(!fs9p_path_is_under_root(arena, ctx, parent_canonical))
					{
						result.valid = 0;
						result.error = str8_lit("path escapes root");
						return result;
					}
					String8 expected_canonical = fs9p_path_join(arena, parent_canonical, name);
					if(root_canonical.size > expected_canonical.size)
					{
						result.valid = 0;
						result.error = str8_lit("path escapes root");
						return result;
					}
					String8 expected_prefix = str8_prefix(expected_canonical, root_canonical.size);
					if(!str8_match(expected_prefix, root_canonical, 0))
					{
						result.valid = 0;
						result.error = str8_lit("path escapes root");
						return result;
					}
					if(expected_canonical.size > root_canonical.size && expected_canonical.str[root_canonical.size] != '/')
					{
						result.valid = 0;
						result.error = str8_lit("path escapes root");
						return result;
					}
				}
				else
				{
					if(joined.size > 0 && joined.str[0] == '/')
					{
						result.valid = 0;
						result.error = str8_lit("path escapes root");
						return result;
					}
				}
			}
			result.absolute_path = joined;
			result.valid = 1;
			return result;
		}
		// Paths don't match, so realpath() succeeded but path is outside root
		result.valid = 0;
		result.error = str8_lit("path escapes root");
		return result;
	}

	String8 root_canonical = os_full_path_from_path(arena, ctx->root_path);
	if(root_canonical.size > 0 && canonical.size >= root_canonical.size)
	{
		if(canonical.size == root_canonical.size)
		{
			result.absolute_path = str8_zero();
		}
		else
		{
			result.absolute_path = str8_skip(canonical, root_canonical.size + 1);
			result.absolute_path = str8_copy(arena, result.absolute_path);
		}
	}
	else
	{
		result.absolute_path = joined;
	}

	result.valid = 1;
	return result;
}

internal String8
os_path_from_fs9p_path(Arena *arena, FsContext9P *ctx, String8 relative_path)
{
	return fs9p_path_join(arena, ctx->root_path, relative_path);
}

////////////////////////////////
//~ File Operations

internal FsHandle9P *
fs9p_open(Arena *arena, FsContext9P *ctx, String8 path, u32 mode)
{
	StorageBackend9P backend = fs9p_get_backend(path);
	FsHandle9P *handle = push_array(arena, FsHandle9P, 1);
	handle->path = str8_copy(arena, path);
	handle->fd = -1;
	handle->ctx = ctx;

	if(backend == StorageBackend9P_ArenaTemp)
	{
		TempNode9P *node = temp9p_open(ctx, path);
		if(node != 0)
		{
			handle->tmp_node = node;
			handle->is_directory = node->is_directory;
			if(mode & P9_OpenFlag_Truncate && !node->is_directory)
			{
				node->content.size = 0;
			}
		}
		return handle;
	}

	String8 os_path = os_path_from_fs9p_path(arena, ctx, path);
	String8 cpath = str8_copy(arena, os_path);
	struct stat st = {0};
	if(stat((char *)cpath.str, &st) == 0 && S_ISDIR(st.st_mode))
	{
		handle->is_directory = 1;
		return handle;
	}

	int flags = 0;
	u32 access_mode = mode & 3;
	if(access_mode == P9_OpenFlag_Read)
	{
		flags = O_RDONLY;
	}
	else if(access_mode == P9_OpenFlag_Write)
	{
		flags = O_WRONLY;
	}
	else if(access_mode == P9_OpenFlag_ReadWrite)
	{
		flags = O_RDWR;
	}

	if(mode & P9_OpenFlag_Truncate)
	{
		flags |= O_TRUNC;
	}

	int fd = open((char *)cpath.str, flags);
	if(fd < 0)
	{
		handle->fd = -1;
		return handle;
	}

	handle->fd = fd;
	return handle;
}

internal void
fs9p_close(FsHandle9P *handle)
{
	if(handle->fd >= 0)
	{
		close(handle->fd);
		handle->fd = -1;
	}
	if(handle->dir_handle != 0)
	{
		closedir((DIR *)handle->dir_handle);
		handle->dir_handle = 0;
	}
}

internal String8
fs9p_read(Arena *arena, FsHandle9P *handle, u64 offset, u64 count)
{
	if(handle->tmp_node != 0)
	{
		return temp9p_read(arena, handle->tmp_node, offset, count);
	}

	if(handle->fd < 0)
	{
		return str8_zero();
	}

	if(lseek(handle->fd, offset, SEEK_SET) != (off_t)offset)
	{
		return str8_zero();
	}

	u8 *buffer = push_array(arena, u8, count);
	ssize_t bytes_read = read(handle->fd, buffer, count);
	if(bytes_read < 0)
	{
		return str8_zero();
	}

	return str8(buffer, bytes_read);
}

internal u64
fs9p_write(FsHandle9P *handle, u64 offset, String8 data)
{
	if(handle->tmp_node != 0 && handle->ctx != 0)
	{
		return temp9p_write(handle->ctx->tmp_arena, handle->tmp_node, offset, data);
	}

	if(handle->fd < 0)
	{
		return 0;
	}

	if(lseek(handle->fd, offset, SEEK_SET) != (off_t)offset)
	{
		return 0;
	}

	ssize_t bytes_written = write(handle->fd, data.str, data.size);
	if(bytes_written < 0)
	{
		return 0;
	}

	return bytes_written;
}

internal b32
fs9p_create(FsContext9P *ctx, String8 path, u32 permissions, u32 mode)
{
	StorageBackend9P backend = fs9p_get_backend(path);
	if(backend == StorageBackend9P_ArenaTemp)
	{
		return temp9p_create(ctx->tmp_arena, ctx, path, permissions);
	}

	Temp scratch = scratch_begin(0, 0);
	String8 os_path = os_path_from_fs9p_path(scratch.arena, ctx, path);
	String8 cpath = str8_copy(scratch.arena, os_path);
	b32 result = 0;

	if(permissions & P9_ModeFlag_Directory)
	{
		if(mkdir((char *)cpath.str, permissions & 0777) == 0)
		{
			result = 1;
		}
	}
	else
	{
		int flags = O_CREAT | O_EXCL;
		u32 access_mode = mode & 3;
		if(access_mode == P9_OpenFlag_Read)
		{
			flags |= O_RDONLY;
		}
		else if(access_mode == P9_OpenFlag_Write)
		{
			flags |= O_WRONLY;
		}
		else if(access_mode == P9_OpenFlag_ReadWrite)
		{
			flags |= O_RDWR;
		}

		int fd = open((char *)cpath.str, flags, permissions & 0777);
		if(fd >= 0)
		{
			close(fd);
			result = 1;
		}
	}

	scratch_end(scratch);
	return result;
}

internal void
fs9p_remove(FsContext9P *ctx, String8 path)
{
	StorageBackend9P backend = fs9p_get_backend(path);
	if(backend == StorageBackend9P_ArenaTemp)
	{
		temp9p_remove(ctx, path);
		return;
	}

	Temp scratch = scratch_begin(0, 0);
	String8 os_path = os_path_from_fs9p_path(scratch.arena, ctx, path);
	String8 cpath = str8_copy(scratch.arena, os_path);

	if(unlink((char *)cpath.str) != 0)
	{
		rmdir((char *)cpath.str);
	}

	scratch_end(scratch);
}

////////////////////////////////
//~ Metadata Operations

internal Dir9P
fs9p_stat(Arena *arena, FsContext9P *ctx, String8 path)
{
	StorageBackend9P backend = fs9p_get_backend(path);
	if(backend == StorageBackend9P_ArenaTemp)
	{
		TempNode9P *node = temp9p_node_lookup(ctx->tmp_root, path);
		return temp9p_stat(arena, node);
	}

	Dir9P dir = dir9p_zero();
	String8 os_path = os_path_from_fs9p_path(arena, ctx, path);
	String8 cpath = str8_copy(arena, os_path);
	struct stat st = {0};
	if(stat((char *)cpath.str, &st) != 0)
	{
		return dir;
	}

	dir.length = st.st_size;
	dir.qid.path = st.st_ino;
	dir.qid.version = st.st_mtime;
	dir.qid.type = S_ISDIR(st.st_mode) ? QidTypeFlag_Directory : QidTypeFlag_File;
	dir.mode = st.st_mode & 0777;
	if(S_ISDIR(st.st_mode))
	{
		dir.mode |= P9_ModeFlag_Directory;
	}
	dir.access_time = st.st_atime;
	dir.modify_time = st.st_mtime;
	dir.user_id = str8_from_uid(arena, st.st_uid);
	dir.group_id = str8_from_gid(arena, st.st_gid);
	dir.modify_user_id = dir.user_id;
	dir.name = fs9p_basename(arena, path);

	return dir;
}

internal b32
fs9p_wstat(FsContext9P *ctx, String8 path, Dir9P *dir)
{
	StorageBackend9P backend = fs9p_get_backend(path);
	if(backend == StorageBackend9P_ArenaTemp)
	{
		TempNode9P *node = temp9p_node_lookup(ctx->tmp_root, path);
		temp9p_wstat(ctx->tmp_arena, node, dir);
		return 1;
	}

	Temp scratch = scratch_begin(0, 0);
	String8 os_path = os_path_from_fs9p_path(scratch.arena, ctx, path);
	String8 cpath = str8_copy(scratch.arena, os_path);
	b32 success = 1;

	if(dir->mode != max_u32)
	{
		mode_t mode_bits = dir->mode & 07777;
		if(chmod((char *)cpath.str, mode_bits) != 0)
		{
			success = 0;
		}
	}

	if(dir->length != max_u64)
	{
		if(truncate((char *)cpath.str, (off_t)dir->length) != 0)
		{
			success = 0;
		}
	}

	if(dir->name.size > 0)
	{
		String8 current_basename = fs9p_basename(scratch.arena, path);
		if(!str8_match(dir->name, current_basename, 0))
		{
			String8 parent_path = fs9p_dirname(scratch.arena, path);
			String8 new_relative_path = fs9p_path_join(scratch.arena, parent_path, dir->name);
			String8 new_os_path = os_path_from_fs9p_path(scratch.arena, ctx, new_relative_path);
			String8 new_cpath = str8_copy(scratch.arena, new_os_path);
			if(rename((char *)cpath.str, (char *)new_cpath.str) != 0)
			{
				success = 0;
			}
			else
			{
				cpath = new_cpath;
			}
		}
	}

	b32 update_times = 0;
	struct timespec times[2] = {0};
	if(dir->access_time != max_u32)
	{
		times[0].tv_sec = dir->access_time;
		times[0].tv_nsec = 0;
		update_times = 1;
	}
	if(dir->modify_time != max_u32)
	{
		times[1].tv_sec = dir->modify_time;
		times[1].tv_nsec = 0;
		update_times = 1;
	}
	if(update_times)
	{
		if(dir->access_time == max_u32)
		{
			struct stat st = {0};
			if(stat((char *)cpath.str, &st) == 0)
			{
				times[0].tv_sec = st.st_atime;
				times[0].tv_nsec = st.st_atim.tv_nsec;
			}
		}
		if(dir->modify_time == max_u32)
		{
			struct stat st = {0};
			if(stat((char *)cpath.str, &st) == 0)
			{
				times[1].tv_sec = st.st_mtime;
				times[1].tv_nsec = st.st_mtim.tv_nsec;
			}
		}
		if(utimensat(AT_FDCWD, (char *)cpath.str, times, 0) != 0)
		{
			success = 0;
		}
	}

	b32 update_owner = 0;
	uid_t uid = (uid_t)-1;
	gid_t gid = (gid_t)-1;

	if(dir->user_id.size > 0)
	{
		if(str8_is_integer(dir->user_id, 10))
		{
			u64 uid_val = u64_from_str8(dir->user_id, 10);
			if(uid_val <= max_u32)
			{
				uid = (uid_t)uid_val;
				update_owner = 1;
			}
		}
		else
		{
			u8 *user_str = push_array(scratch.arena, u8, dir->user_id.size + 1);
			MemoryCopy(user_str, dir->user_id.str, dir->user_id.size);
			user_str[dir->user_id.size] = 0;
			struct passwd *pw = getpwnam((char *)user_str);
			if(pw != 0)
			{
				uid = pw->pw_uid;
				update_owner = 1;
			}
		}
	}

	if(dir->group_id.size > 0)
	{
		if(str8_is_integer(dir->group_id, 10))
		{
			u64 gid_val = u64_from_str8(dir->group_id, 10);
			if(gid_val <= max_u32)
			{
				gid = (gid_t)gid_val;
				update_owner = 1;
			}
		}
		else
		{
			u8 *group_str = push_array(scratch.arena, u8, dir->group_id.size + 1);
			MemoryCopy(group_str, dir->group_id.str, dir->group_id.size);
			group_str[dir->group_id.size] = 0;
			struct group *gr = getgrnam((char *)group_str);
			if(gr != 0)
			{
				gid = gr->gr_gid;
				update_owner = 1;
			}
		}
	}

	if(update_owner)
	{
		if(chown((char *)cpath.str, uid, gid) != 0)
		{
			success = 0;
		}
	}

	scratch_end(scratch);
	return success;
}

////////////////////////////////
//~ Directory Operations

internal DirIterator9P *
fs9p_opendir(Arena *arena, FsContext9P *ctx, String8 path)
{
	StorageBackend9P backend = fs9p_get_backend(path);
	DirIterator9P *iter = push_array(arena, DirIterator9P, 1);
	iter->path = str8_copy(arena, path);

	if(backend == StorageBackend9P_ArenaTemp)
	{
		TempNode9P *node = temp9p_node_lookup(ctx->tmp_root, path);
		if(node != 0 && node->is_directory)
		{
			iter->tmp_node = node;
			iter->tmp_current = node->first_child;
		}
		return iter;
	}

	String8 os_path = os_path_from_fs9p_path(arena, ctx, path);
	String8 cpath = str8_copy(arena, os_path);

	DIR *dir = opendir((char *)cpath.str);
	iter->dir_handle = dir;

	return iter;
}

internal String8
fs9p_readdir(Arena *arena, FsContext9P *ctx, DirIterator9P *iter, u64 offset, u64 count)
{
	if(iter->tmp_node != 0)
	{
		return temp9p_readdir(arena, iter->tmp_node, &iter->tmp_current, offset, count);
	}

	if(iter->dir_handle == 0)
	{
		return str8_zero();
	}

	DIR *dir = (DIR *)iter->dir_handle;
	rewinddir(dir);
	iter->position = 0;

	Temp scratch = scratch_begin(&arena, 1);
	int dir_fd = dirfd(dir);

	u64 buf_cap = Min(count, P9_DIR_ENTRY_MAX);
	u8 *buf = push_array(arena, u8, buf_cap);
	u64 buf_pos = 0;

	for(;;)
	{
		struct dirent *entry = readdir(dir);
		if(entry == 0)
		{
			break;
		}

		String8 name = str8_cstring(entry->d_name);
		if(str8_match(name, str8_lit("."), 0) || str8_match(name, str8_lit(".."), 0))
		{
			continue;
		}

		String8 path = fs9p_path_join(scratch.arena, iter->path, name);
		struct stat st = {0};
		b32 has_stat = 0;

		if(dir_fd >= 0)
		{
			if(fstatat(dir_fd, (char *)name.str, &st, 0) == 0)
			{
				has_stat = 1;
			}
		}

		if(!has_stat)
		{
			String8 os_path = os_path_from_fs9p_path(scratch.arena, ctx, path);
			String8 cpath = str8_copy(scratch.arena, os_path);
			if(stat((char *)cpath.str, &st) == 0)
			{
				has_stat = 1;
			}
		}

		if(!has_stat)
		{
			continue;
		}

		Dir9P dir = dir9p_zero();
		dir.length = st.st_size;
		dir.qid.path = st.st_ino;
		dir.qid.version = st.st_mtime;
		dir.qid.type = S_ISDIR(st.st_mode) ? QidTypeFlag_Directory : QidTypeFlag_File;
		dir.mode = st.st_mode & 0777;
		if(S_ISDIR(st.st_mode))
		{
			dir.mode |= P9_ModeFlag_Directory;
		}
		dir.access_time = st.st_atime;
		dir.modify_time = st.st_mtime;
		dir.user_id = str8_from_uid(scratch.arena, st.st_uid);
		dir.group_id = str8_from_gid(scratch.arena, st.st_gid);
		dir.modify_user_id = dir.user_id;
		dir.name = str8_copy(scratch.arena, name);

		String8 encoded = str8_from_dir9p(scratch.arena, dir);

		if(iter->position + encoded.size <= offset)
		{
			iter->position += encoded.size;
			continue;
		}

		if(buf_pos + encoded.size > count)
		{
			break;
		}

		if(buf_pos + encoded.size > buf_cap)
		{
			u64 new_cap = buf_cap * 2;
			for(; buf_pos + encoded.size > new_cap; new_cap *= 2)
			{
			}
			u8 *new_buf = push_array(arena, u8, new_cap);
			MemoryCopy(new_buf, buf, buf_pos);
			buf = new_buf;
			buf_cap = new_cap;
		}

		MemoryCopy(buf + buf_pos, encoded.str, encoded.size);
		buf_pos += encoded.size;
		iter->position += encoded.size;
	}

	scratch_end(scratch);
	return str8(buf, buf_pos);
}

internal void
fs9p_closedir(DirIterator9P *iter)
{
	if(iter->dir_handle != 0)
	{
		closedir((DIR *)iter->dir_handle);
		iter->dir_handle = 0;
	}
}

////////////////////////////////
//~ Temporary Storage Helpers

internal TempNode9P *
temp9p_node_lookup(TempNode9P *root, String8 path)
{
	if(path.size == 0 || str8_match(path, str8_lit("tmp"), 0))
	{
		return root;
	}

	String8 lookup_path = path;
	if(str8_match(str8_prefix(path, 4), str8_lit("tmp/"), 0))
	{
		lookup_path = str8_skip(path, 4);
	}

	TempNode9P *node = root;
	for(u64 path_pos = 0; path_pos < lookup_path.size && node != 0;)
	{
		u64 component_end = path_pos;
		for(; component_end < lookup_path.size && lookup_path.str[component_end] != '/'; component_end += 1)
		{
		}

		String8 component = str8(lookup_path.str + path_pos, component_end - path_pos);
		b32 found = 0;
		for(TempNode9P *child = node->first_child; child != 0; child = child->next_sibling)
		{
			if(str8_match(child->name, component, 0))
			{
				node = child;
				found = 1;
				break;
			}
		}

		if(!found)
		{
			return 0;
		}

		path_pos = component_end;
		if(path_pos < lookup_path.size && lookup_path.str[path_pos] == '/')
		{
			path_pos += 1;
		}
	}

	return node;
}

internal TempNode9P *
temp9p_node_create(Arena *arena, FsContext9P *ctx, String8 path, String8 name, b32 is_dir, u32 mode)
{
	TempNode9P *node = push_array(arena, TempNode9P, 1);
	node->path = str8_copy(arena, path);
	node->name = str8_copy(arena, name);
	node->content = str8_zero();
	node->user_id = str8_lit("nobody");
	node->group_id = str8_lit("nobody");
	node->qid.path = ctx->tmp_qid_count;
	ctx->tmp_qid_count += 1;
	node->qid.version = 0;
	node->qid.type = is_dir ? QidTypeFlag_Directory : QidTypeFlag_File;
	node->mode = mode;
	node->access_time = os_now_unix();
	node->modify_time = node->access_time;
	node->is_directory = is_dir;
	return node;
}

////////////////////////////////
//~ Temporary Storage Operations

internal b32
temp9p_create(Arena *arena, FsContext9P *ctx, String8 path, u32 permissions)
{
	String8 parent_path = fs9p_dirname(arena, path);
	String8 name = fs9p_basename(arena, path);
	TempNode9P *parent = temp9p_node_lookup(ctx->tmp_root, parent_path);
	if(parent == 0 || !parent->is_directory)
	{
		return 0;
	}

	for(TempNode9P *child = parent->first_child; child != 0; child = child->next_sibling)
	{
		if(str8_match(child->name, name, 0))
		{
			return 0;
		}
	}

	b32 is_dir = (permissions & P9_ModeFlag_Directory) != 0;
	TempNode9P *node = temp9p_node_create(ctx->tmp_arena, ctx, path, name, is_dir, permissions & 0777);
	node->parent = parent;
	node->next_sibling = parent->first_child;
	parent->first_child = node;
	return 1;
}

internal TempNode9P *
temp9p_open(FsContext9P *ctx, String8 path)
{
	return temp9p_node_lookup(ctx->tmp_root, path);
}

internal String8
temp9p_read(Arena *arena, TempNode9P *node, u64 offset, u64 count)
{
	if(node == 0 || node->is_directory || offset >= node->content.size)
	{
		return str8_zero();
	}
	u64 read_size = Min(node->content.size - offset, count);
	return str8_copy(arena, str8(node->content.str + offset, read_size));
}

internal u64
temp9p_write(Arena *arena, TempNode9P *node, u64 offset, String8 data)
{
	if(node == 0 || node->is_directory)
	{
		return 0;
	}

	u64 new_size = offset + data.size;
	if(new_size > node->content.size)
	{
		u8 *new_buf = push_array(arena, u8, new_size);
		if(node->content.size > 0)
		{
			MemoryCopy(new_buf, node->content.str, node->content.size);
		}
		if(offset > node->content.size)
		{
			MemoryZero(new_buf + node->content.size, offset - node->content.size);
		}
		node->content.str = new_buf;
		node->content.size = new_size;
	}

	MemoryCopy(node->content.str + offset, data.str, data.size);
	node->modify_time = os_now_unix();
	node->qid.version += 1;
	return data.size;
}

internal void
temp9p_remove(FsContext9P *ctx, String8 path)
{
	TempNode9P *node = temp9p_node_lookup(ctx->tmp_root, path);
	if(node == 0 || node->parent == 0)
	{
		return;
	}

	TempNode9P *parent = node->parent;
	if(parent->first_child == node)
	{
		parent->first_child = node->next_sibling;
	}
	else
	{
		for(TempNode9P *prev = parent->first_child; prev != 0; prev = prev->next_sibling)
		{
			if(prev->next_sibling == node)
			{
				prev->next_sibling = node->next_sibling;
				break;
			}
		}
	}
}

internal Dir9P
temp9p_stat(Arena *arena, TempNode9P *node)
{
	Dir9P dir = dir9p_zero();
	if(node == 0)
	{
		return dir;
	}

	dir.length = node->content.size;
	dir.qid = node->qid;
	dir.mode = node->mode;
	if(node->is_directory)
	{
		dir.mode |= P9_ModeFlag_Directory;
	}
	dir.access_time = node->access_time;
	dir.modify_time = node->modify_time;
	dir.user_id = str8_copy(arena, node->user_id);
	dir.group_id = str8_copy(arena, node->group_id);
	dir.modify_user_id = dir.user_id;
	dir.name = str8_copy(arena, node->name);

	return dir;
}

internal void
temp9p_wstat(Arena *arena, TempNode9P *node, Dir9P *dir)
{
	if(node == 0)
	{
		return;
	}

	if(dir->mode != max_u32)
	{
		node->mode = dir->mode & 07777;
	}

	if(dir->length != max_u64)
	{
		if(dir->length < node->content.size)
		{
			node->content.size = dir->length;
		}
		else if(dir->length > node->content.size)
		{
			u8 *new_content = push_array(arena, u8, dir->length);
			if(node->content.size > 0)
			{
				MemoryCopy(new_content, node->content.str, node->content.size);
			}
			MemoryZero(new_content + node->content.size, dir->length - node->content.size);
			node->content.str = new_content;
			node->content.size = dir->length;
		}
	}

	if(dir->access_time != max_u32)
	{
		node->access_time = dir->access_time;
	}

	if(dir->modify_time != max_u32)
	{
		node->modify_time = dir->modify_time;
	}

	if(dir->name.size > 0 && !str8_match(dir->name, node->name, 0))
	{
		String8 parent_path = fs9p_dirname(arena, node->path);
		String8 new_path = fs9p_path_join(arena, parent_path, dir->name);
		node->path = new_path;
		node->name = str8_copy(arena, dir->name);
	}
}

internal String8
temp9p_readdir(Arena *arena, TempNode9P *node, TempNode9P **iter, u64 offset, u64 count)
{
	if(node == 0 || !node->is_directory)
	{
		return str8_zero();
	}

	*iter = node->first_child;
	u64 pos = 0;

	u64 buf_cap = Min(count, P9_DIR_ENTRY_MAX);
	u8 *buf = push_array(arena, u8, buf_cap);
	u64 buf_pos = 0;
	Temp scratch = scratch_begin(&arena, 1);

	for(; *iter != 0; *iter = (*iter)->next_sibling)
	{
		TempNode9P *child = *iter;
		Dir9P dir = dir9p_zero();
		dir.length = child->content.size;
		dir.qid = child->qid;
		dir.mode = child->mode;
		if(child->is_directory)
		{
			dir.mode |= P9_ModeFlag_Directory;
		}
		dir.access_time = child->access_time;
		dir.modify_time = child->modify_time;
		dir.user_id = str8_copy(scratch.arena, child->user_id);
		dir.group_id = str8_copy(scratch.arena, child->group_id);
		dir.modify_user_id = dir.user_id;
		dir.name = str8_copy(scratch.arena, child->name);

		String8 encoded = str8_from_dir9p(scratch.arena, dir);

		if(pos + encoded.size <= offset)
		{
			pos += encoded.size;
			continue;
		}

		if(buf_pos + encoded.size > count)
		{
			break;
		}

		if(buf_pos + encoded.size > buf_cap)
		{
			u64 new_cap = buf_cap * 2;
			for(; buf_pos + encoded.size > new_cap; new_cap *= 2)
			{
			}
			u8 *new_buf = push_array(arena, u8, new_cap);
			MemoryCopy(new_buf, buf, buf_pos);
			buf = new_buf;
			buf_cap = new_cap;
		}

		MemoryCopy(buf + buf_pos, encoded.str, encoded.size);
		buf_pos += encoded.size;
	}

	scratch_end(scratch);
	return str8(buf, buf_pos);
}

////////////////////////////////
//~ Backend Routing

internal StorageBackend9P
fs9p_get_backend(String8 path)
{
	if(str8_match(path, str8_lit("tmp"), 0) || (path.size >= 4 && str8_match(str8_prefix(path, 4), str8_lit("tmp/"), 0)))
	{
		return StorageBackend9P_ArenaTemp;
	}
	return StorageBackend9P_Disk;
}

////////////////////////////////
//~ UID/GID Conversion

internal String8
str8_from_uid(Arena *arena, u32 uid)
{
	struct passwd *pw = getpwuid((uid_t)uid);
	if(pw != 0 && pw->pw_name != 0)
	{
		return str8_copy(arena, str8_cstring(pw->pw_name));
	}
	return str8_from_u64(arena, uid, 10, 0, 0);
}

internal String8
str8_from_gid(Arena *arena, u32 gid)
{
	struct group *gr = getgrgid((gid_t)gid);
	if(gr != 0 && gr->gr_name != 0)
	{
		return str8_copy(arena, str8_cstring(gr->gr_name));
	}
	return str8_from_u64(arena, gid, 10, 0, 0);
}
