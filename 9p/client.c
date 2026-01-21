////////////////////////////////
//~ Client Connection

internal Client9P *
client9p_init(Arena *arena, u64 fd)
{
	Client9P *client = push_array(arena, Client9P, 1);
	client->fd = fd;
	client->next_tag = 1;
	client->next_fid = 1;
	if(!client9p_version(arena, client, P9_IOUNIT_DEFAULT))
	{
		client9p_unmount(arena, client);
		return 0;
	}
	return client;
}

internal String8
get_user_name(Arena *arena)
{
	uid_t uid = getuid();
	struct passwd *pw = getpwuid(uid);
	if(pw == 0)
	{
		return str8_lit("none");
	}
	String8 name = str8_cstring(pw->pw_name);
	return str8_copy(arena, name);
}

internal ClientFid9P *
client9p_auth(Arena *arena, Client9P *server_client, String8 user, String8 attach_path)
{
	Temp scratch = scratch_begin(&arena, 1);

	OS_Handle auth_handle =
	    dial9p_connect(scratch.arena, str8_lit("unix!/var/run/9auth"), str8_lit("unix"), str8_lit("9auth"));
	if(os_handle_match(auth_handle, os_handle_zero()))
	{
		scratch_end(scratch);
		return 0;
	}

	u64 auth_fd = auth_handle.u64[0];
	Client9P *auth_client = client9p_init(arena, auth_fd);
	if(auth_client == 0)
	{
		os_file_close(auth_handle);
		scratch_end(scratch);
		return 0;
	}

	ClientFid9P *auth_root = client9p_attach(arena, auth_client, P9_FID_NONE, user, str8_lit("/"));
	if(auth_root == 0)
	{
		os_file_close(auth_handle);
		scratch_end(scratch);
		return 0;
	}

	ClientFid9P *rpc_fid = client9p_fid_walk(arena, auth_root, str8_lit("rpc"));
	if(rpc_fid == 0)
	{
		os_file_close(auth_handle);
		scratch_end(scratch);
		return 0;
	}

	if(!client9p_fid_open(arena, rpc_fid, P9_OpenFlag_ReadWrite))
	{
		os_file_close(auth_handle);
		scratch_end(scratch);
		return 0;
	}

	ClientFid9P *server_auth_fid = client9p_tauth(arena, server_client, user, attach_path);
	if(server_auth_fid == 0)
	{
		os_file_close(auth_handle);
		scratch_end(scratch);
		return 0;
	}

	String8 start_cmd = str8f(scratch.arena, "start proto=fido2 role=client user=%S server=%S", user, attach_path);
	s64 write_result = client9p_fid_pwrite(arena, rpc_fid, (void *)start_cmd.str, start_cmd.size, 0);
	if(write_result != (s64)start_cmd.size)
	{
		os_file_close(auth_handle);
		scratch_end(scratch);
		return 0;
	}

	u8 challenge[32];
	s64 challenge_len = client9p_fid_pread(arena, server_auth_fid, challenge, 32, 0);
	if(challenge_len != 32)
	{
		os_file_close(auth_handle);
		scratch_end(scratch);
		return 0;
	}

	write_result = client9p_fid_pwrite(arena, rpc_fid, challenge, 32, 0);
	if(write_result != 32)
	{
		os_file_close(auth_handle);
		scratch_end(scratch);
		return 0;
	}

	u8 auth_response[512];
	s64 auth_response_len = client9p_fid_pread(arena, rpc_fid, auth_response, 512, 0);
	if(auth_response_len <= 0)
	{
		os_file_close(auth_handle);
		scratch_end(scratch);
		return 0;
	}

	write_result = client9p_fid_pwrite(arena, server_auth_fid, auth_response, auth_response_len, 0);
	if(write_result != auth_response_len)
	{
		os_file_close(auth_handle);
		scratch_end(scratch);
		return 0;
	}

	u8 done_response[16];
	s64 done_len = client9p_fid_pread(arena, server_auth_fid, done_response, 16, 0);
	if(done_len <= 0)
	{
		os_file_close(auth_handle);
		scratch_end(scratch);
		return 0;
	}

	client9p_fid_close(arena, rpc_fid);
	os_file_close(auth_handle);
	scratch_end(scratch);

	return server_auth_fid;
}

internal Client9P *
client9p_mount(Arena *arena, u64 fd, String8 attach_path, b32 use_auth)
{
	Client9P *client = client9p_init(arena, fd);
	if(client == 0)
	{
		return 0;
	}
	String8 user = get_user_name(arena);

	u32 auth_fid_num = P9_FID_NONE;
	if(use_auth)
	{
		ClientFid9P *auth_fid = client9p_auth(arena, client, user, attach_path);
		if(auth_fid != 0)
		{
			auth_fid_num = auth_fid->fid;
		}
		else
		{
			client9p_unmount(arena, client);
			return 0;
		}
	}

	ClientFid9P *fid = client9p_attach(arena, client, auth_fid_num, user, attach_path);
	if(fid == 0)
	{
		client9p_unmount(arena, client);
		return 0;
	}
	client->root = fid;
	return client;
}

internal void
client9p_unmount(Arena *arena, Client9P *client)
{
	client9p_fid_close(arena, client->root);
	client->root = 0;
	close(client->fd);
	client->fd = -1;
}

internal Message9P
client9p_rpc(Arena *arena, Client9P *client, Message9P tx)
{
	Message9P result = msg9p_zero();
	if(tx.type != Msg9P_Tversion)
	{
		tx.tag = client->next_tag;
		client->next_tag += 1;
		if(client->next_tag == P9_TAG_NONE)
		{
			client->next_tag = 1;
		}
	}
	Temp scratch = scratch_begin(&arena, 1);
	log_infof("9P <- %S\n", str8_from_msg9p__fmt(scratch.arena, tx));
	String8 tx_msg = str8_from_msg9p(arena, tx);
	if(tx_msg.size == 0)
	{
		scratch_end(scratch);
		return result;
	}
	ssize_t bytes_written = write(client->fd, tx_msg.str, tx_msg.size);
	if(bytes_written < 0 || (u64)bytes_written != tx_msg.size)
	{
		scratch_end(scratch);
		return result;
	}
	String8 rx_msg = read_9p_msg(arena, client->fd);
	if(rx_msg.size == 0)
	{
		scratch_end(scratch);
		return result;
	}
	Message9P rx = msg9p_from_str8(rx_msg);
	log_infof("9P -> %S\n", str8_from_msg9p__fmt(scratch.arena, rx));
	if(rx.type == 0 || rx.type == Msg9P_Rerror || rx.type != tx.type + 1)
	{
		scratch_end(scratch);
		return result;
	}
	if(rx.tag != tx.tag)
	{
		scratch_end(scratch);
		return result;
	}
	scratch_end(scratch);
	return rx;
}

internal b32
client9p_version(Arena *arena, Client9P *client, u32 max_message_size)
{
	Message9P tx = msg9p_zero();
	tx.type = Msg9P_Tversion;
	tx.tag = P9_TAG_NONE;
	tx.max_message_size = max_message_size;
	tx.protocol_version = version_9p;
	Message9P rx = client9p_rpc(arena, client, tx);
	if(rx.type != Msg9P_Rversion)
	{
		return 0;
	}
	client->max_message_size = rx.max_message_size;
	if(!str8_match(rx.protocol_version, version_9p, 0))
	{
		return 0;
	}
	return 1;
}

internal ClientFid9P *
client9p_tauth(Arena *arena, Client9P *client, String8 user_name, String8 attach_path)
{
	ClientFid9P *auth_fid_result = push_array(arena, ClientFid9P, 1);
	auth_fid_result->fid = client->next_fid;
	client->next_fid += 1;
	auth_fid_result->client = client;
	Message9P tx = msg9p_zero();
	tx.type = Msg9P_Tauth;
	tx.auth_fid = auth_fid_result->fid;
	tx.user_name = user_name;
	tx.attach_path = attach_path;
	Message9P rx = client9p_rpc(arena, client, tx);
	if(rx.type != Msg9P_Rauth)
	{
		return 0;
	}
	auth_fid_result->qid = rx.auth_qid;
	return auth_fid_result;
}

internal ClientFid9P *
client9p_attach(Arena *arena, Client9P *client, u32 auth_fid, String8 user_name, String8 attach_path)
{
	ClientFid9P *fid = push_array(arena, ClientFid9P, 1);
	fid->fid = client->next_fid;
	client->next_fid += 1;
	fid->client = client;
	Message9P tx = msg9p_zero();
	tx.type = Msg9P_Tattach;
	tx.fid = fid->fid;
	tx.auth_fid = auth_fid;
	tx.user_name = user_name;
	tx.attach_path = attach_path;
	Message9P rx = client9p_rpc(arena, client, tx);
	if(rx.type != Msg9P_Rattach)
	{
		return 0;
	}
	fid->qid = rx.qid;
	return fid;
}

////////////////////////////////
//~ Fid Operations

internal void
client9p_fid_close(Arena *arena, ClientFid9P *fid)
{
	if(fid == 0)
	{
		return;
	}
	Message9P tx = msg9p_zero();
	tx.type = Msg9P_Tclunk;
	tx.fid = fid->fid;
	client9p_rpc(arena, fid->client, tx);
}

internal ClientFid9P *
client9p_fid_walk(Arena *arena, ClientFid9P *fid, String8 path)
{
	if(fid == 0)
	{
		return 0;
	}
	ClientFid9P *walk_fid = push_array(arena, ClientFid9P, 1);
	Temp scratch = temp_begin(arena);
	walk_fid->fid = fid->client->next_fid;
	fid->client->next_fid += 1;
	walk_fid->qid = fid->qid;
	walk_fid->client = fid->client;
	b32 first_walk = 1;
	String8List parts = str8_split(scratch.arena, path, (u8 *)"/", 1, 0);
	String8Node *node = parts.first;
	Message9P tx = msg9p_zero();
	tx.type = Msg9P_Twalk;
	tx.fid = fid->fid;
	tx.new_fid = walk_fid->fid;
	if(node == 0)
	{
		Message9P rx = client9p_rpc(arena, fid->client, tx);
		if(rx.type != Msg9P_Rwalk || rx.walk_qid_count != tx.walk_name_count)
		{
			return 0;
		}
		return walk_fid;
	}
	for(; node != 0;)
	{
		tx.fid = first_walk ? fid->fid : walk_fid->fid;
		u64 i = 0;
		for(; node != 0 && i < P9_MAX_WALK_ELEM_COUNT;)
		{
			String8 part = node->string;
			if(str8_match(part, str8_lit("."), 0))
			{
				node = node->next;
				continue;
			}
			tx.walk_names[i] = part;
			i += 1;
			node = node->next;
		}
		tx.walk_name_count = i;
		Message9P rx = client9p_rpc(arena, fid->client, tx);
		if(rx.type != Msg9P_Rwalk || rx.walk_qid_count != tx.walk_name_count)
		{
			if(!first_walk)
			{
				client9p_fid_close(arena, walk_fid);
			}
			return 0;
		}
		if(rx.walk_qid_count > 0)
		{
			walk_fid->qid = rx.walk_qids[rx.walk_qid_count - 1];
		}
		first_walk = 0;
	}
	return walk_fid;
}

internal b32
client9p_fid_create(Arena *arena, ClientFid9P *fid, String8 name, u32 mode, u32 permissions)
{
	if(fid == 0)
	{
		return 0;
	}
	Message9P tx = msg9p_zero();
	tx.type = Msg9P_Tcreate;
	tx.fid = fid->fid;
	tx.name = name;
	tx.permissions = permissions;
	tx.open_mode = mode;
	Message9P rx = client9p_rpc(arena, fid->client, tx);
	if(rx.type != Msg9P_Rcreate)
	{
		return 0;
	}
	fid->mode = mode;
	return 1;
}

internal ClientFid9P *
client9p_create(Arena *arena, Client9P *client, String8 name, u32 mode, u32 permissions)
{
	if(client == 0 || client->root == 0)
	{
		return 0;
	}
	String8 dir = str8_chop_last_slash(name);
	String8 element = str8_skip_last_slash(name);
	ClientFid9P *fid = client9p_fid_walk(arena, client->root, dir);
	if(fid == 0)
	{
		return 0;
	}
	if(!client9p_fid_create(arena, fid, element, mode, permissions))
	{
		client9p_fid_close(arena, fid);
		return 0;
	}
	return fid;
}

internal b32
client9p_fid_remove(Arena *arena, ClientFid9P *fid)
{
	if(fid == 0)
	{
		return 0;
	}
	Message9P tx = msg9p_zero();
	tx.type = Msg9P_Tremove;
	tx.fid = fid->fid;
	Message9P rx = client9p_rpc(arena, fid->client, tx);
	if(rx.type != Msg9P_Rremove)
	{
		return 0;
	}
	return 1;
}

internal b32
client9p_remove(Arena *arena, Client9P *client, String8 name)
{
	if(client == 0 || client->root == 0)
	{
		return 0;
	}
	ClientFid9P *fid = client9p_fid_walk(arena, client->root, name);
	if(fid == 0)
	{
		return 0;
	}
	if(!client9p_fid_remove(arena, fid))
	{
		return 0;
	}
	return 1;
}

internal b32
client9p_fid_open(Arena *arena, ClientFid9P *fid, u32 mode)
{
	if(fid == 0)
	{
		return 0;
	}
	Message9P tx = msg9p_zero();
	tx.type = Msg9P_Topen;
	tx.fid = fid->fid;
	tx.open_mode = mode;
	Message9P rx = client9p_rpc(arena, fid->client, tx);
	if(rx.type != Msg9P_Ropen)
	{
		return 0;
	}
	fid->mode = mode;
	return 1;
}

internal ClientFid9P *
client9p_open(Arena *arena, Client9P *client, String8 name, u32 mode)
{
	if(client == 0 || client->root == 0)
	{
		return 0;
	}
	ClientFid9P *fid = client9p_fid_walk(arena, client->root, name);
	if(fid == 0)
	{
		return 0;
	}
	if(!client9p_fid_open(arena, fid, mode))
	{
		client9p_fid_close(arena, fid);
		return 0;
	}
	return fid;
}

internal s64
client9p_fid_pread(Arena *arena, ClientFid9P *fid, void *buf, u64 n, s64 offset)
{
	if(fid == 0 || buf == 0)
	{
		return -1;
	}
	u32 max_message_size = fid->client->max_message_size - P9_MESSAGE_HEADER_SIZE;
	u64 total_num_bytes_to_read = n;
	u64 total_num_bytes_read = 0;
	u64 total_num_bytes_left_to_read = total_num_bytes_to_read;
	s64 current_offset = (offset == -1) ? fid->offset : offset;
	for(; total_num_bytes_left_to_read > 0;)
	{
		u64 chunk_size = total_num_bytes_left_to_read;
		if(chunk_size > max_message_size)
		{
			chunk_size = max_message_size;
		}
		Message9P tx = msg9p_zero();
		tx.type = Msg9P_Tread;
		tx.fid = fid->fid;
		tx.file_offset = current_offset;
		tx.byte_count = chunk_size;
		Message9P rx = client9p_rpc(arena, fid->client, tx);
		if(rx.type != Msg9P_Rread)
		{
			if(total_num_bytes_read == 0)
			{
				return -1;
			}
			break;
		}
		s64 read_result = rx.payload_data.size;
		if(read_result > (s64)chunk_size)
		{
			read_result = chunk_size;
		}
		if(read_result > 0)
		{
			MemoryCopy((u8 *)buf + total_num_bytes_read, rx.payload_data.str, read_result);
			total_num_bytes_read += read_result;
			total_num_bytes_left_to_read -= read_result;
			current_offset += read_result;
			if(offset == -1)
			{
				fid->offset += read_result;
			}
		}
		if(read_result < (s64)chunk_size)
		{
			break;
		}
	}
	return total_num_bytes_read;
}

internal s64
client9p_fid_read_range(Arena *arena, ClientFid9P *fid, void *buf, Rng1U64 range)
{
	if(fid == 0 || buf == 0)
	{
		return -1;
	}
	u64 num_bytes = dim_1u64(range);
	return client9p_fid_pread(arena, fid, buf, num_bytes, range.min);
}

internal s64
client9p_fid_pwrite(Arena *arena, ClientFid9P *fid, void *buf, u64 n, s64 offset)
{
	if(fid == 0 || buf == 0)
	{
		return -1;
	}
	u32 max_message_size = fid->client->max_message_size - P9_MESSAGE_HEADER_SIZE;
	u64 total_num_bytes_to_write = n;
	u64 total_num_bytes_written = 0;
	u64 total_num_bytes_left_to_write = total_num_bytes_to_write;
	for(; total_num_bytes_left_to_write > 0;)
	{
		u64 write_size = total_num_bytes_left_to_write;
		if(write_size > max_message_size)
		{
			write_size = max_message_size;
		}
		Message9P tx = msg9p_zero();
		tx.type = Msg9P_Twrite;
		tx.fid = fid->fid;
		tx.file_offset = (offset == -1) ? fid->offset : offset + total_num_bytes_written;
		tx.payload_data.size = write_size;
		tx.payload_data.str = (u8 *)buf + total_num_bytes_written;
		Message9P rx = client9p_rpc(arena, fid->client, tx);
		if(rx.type != Msg9P_Rwrite)
		{
			if(total_num_bytes_written == 0)
			{
				return -1;
			}
			break;
		}
		u32 write_result = rx.byte_count;
		if(write_result == 0)
		{
			if(total_num_bytes_written == 0)
			{
				return -1;
			}
			break;
		}
		total_num_bytes_written += write_result;
		total_num_bytes_left_to_write -= write_result;
		if(offset == -1)
		{
			fid->offset += write_result;
		}
		if(write_result < write_size)
		{
			break;
		}
	}
	return total_num_bytes_written;
}

internal s64
client9p_fid_write_range(Arena *arena, ClientFid9P *fid, void *buf, Rng1U64 range)
{
	if(fid == 0 || buf == 0)
	{
		return -1;
	}
	u64 num_bytes = dim_1u64(range);
	return client9p_fid_pwrite(arena, fid, buf, num_bytes, range.min);
}

internal DirList9P
client9p_dir_list_from_str8(Arena *arena, String8 buffer)
{
	DirList9P result = {0};
	u64 offset = 0;
	for(; offset < buffer.size;)
	{
		if(offset + 2 > buffer.size)
		{
			return result;
		}
		u64 entry_size = 2 + from_le_u16(read_u16(&buffer.str[offset]));
		if(offset + entry_size > buffer.size)
		{
			return result;
		}
		String8 dir_msg = str8_zero();
		dir_msg.str = &buffer.str[offset];
		dir_msg.size = entry_size;
		Dir9P dir = dir9p_from_str8(dir_msg);
		if(dir.name.size == 0 && entry_size > 2)
		{
			return result;
		}
		dir9p_list_push(arena, &result, dir);
		offset += entry_size;
	}
	return result;
}

internal DirList9P
client9p_fid_read_dirs(Arena *arena, ClientFid9P *fid)
{
	DirList9P result = {0};
	if(fid == 0)
	{
		return result;
	}
	u8 *buf = push_array_no_zero(arena, u8, P9_DIR_BUFFER_MAX);
	s64 total_bytes_read = 0;
	u64 buffer_space_left = P9_DIR_BUFFER_MAX;
	for(; buffer_space_left >= P9_DIR_ENTRY_MAX;)
	{
		s64 bytes_read = client9p_fid_pread(arena, fid, buf + total_bytes_read, P9_DIR_ENTRY_MAX, -1);
		if(bytes_read <= 0)
		{
			break;
		}
		total_bytes_read += bytes_read;
		buffer_space_left -= bytes_read;
	}
	String8 buffer = str8(buf, total_bytes_read);
	result = client9p_dir_list_from_str8(arena, buffer);
	return result;
}

internal Dir9P
client9p_fid_stat(Arena *arena, ClientFid9P *fid)
{
	Dir9P result = dir9p_zero();
	if(fid == 0)
	{
		return result;
	}
	Message9P tx = msg9p_zero();
	tx.type = Msg9P_Tstat;
	tx.fid = fid->fid;
	Message9P rx = client9p_rpc(arena, fid->client, tx);
	if(rx.type != Msg9P_Rstat)
	{
		return result;
	}
	result = dir9p_from_str8(rx.stat_data);
	return result;
}

internal Dir9P
client9p_stat(Arena *arena, Client9P *client, String8 name)
{
	Dir9P result = dir9p_zero();
	if(client == 0 || client->root == 0)
	{
		return result;
	}
	ClientFid9P *fid = client9p_fid_walk(arena, client->root, name);
	if(fid == 0)
	{
		return result;
	}
	result = client9p_fid_stat(arena, fid);
	client9p_fid_close(arena, fid);
	return result;
}

internal b32
client9p_fid_wstat(Arena *arena, ClientFid9P *fid, Dir9P dir)
{
	if(fid == 0)
	{
		return 0;
	}
	Temp scratch = temp_begin(arena);
	String8 stat = str8_from_dir9p(scratch.arena, dir);
	if(stat.size == 0)
	{
		return 0;
	}
	Message9P tx = msg9p_zero();
	tx.type = Msg9P_Twstat;
	tx.fid = fid->fid;
	tx.stat_data = stat;
	Message9P rx = client9p_rpc(arena, fid->client, tx);
	if(rx.type != Msg9P_Rwstat)
	{
		return 0;
	}
	return 1;
}

internal b32
client9p_wstat(Arena *arena, Client9P *client, String8 name, Dir9P dir)
{
	if(client == 0 || client->root == 0)
	{
		return 0;
	}
	ClientFid9P *fid = client9p_fid_walk(arena, client->root, name);
	if(fid == 0)
	{
		return 0;
	}
	b32 result = client9p_fid_wstat(arena, fid, dir);
	client9p_fid_close(arena, fid);
	return result;
}

internal b32
client9p_access(Arena *arena, Client9P *client, String8 name, u32 mode)
{
	if(client == 0 || client->root == 0)
	{
		return 0;
	}
	if(mode == P9_AccessFlag_Exist)
	{
		Dir9P dir = client9p_stat(arena, client, name);
		if(dir.name.size == 0)
		{
			return 0;
		}
		return 1;
	}
	ClientFid9P *fid = client9p_open(arena, client, name, open_mode_table[mode & 7]);
	if(fid == 0)
	{
		return 0;
	}
	client9p_fid_close(arena, fid);
	return 1;
}

internal s64
client9p_fid_seek(Arena *arena, ClientFid9P *fid, s64 offset, u32 type)
{
	if(fid == 0)
	{
		return -1;
	}
	s64 pos = 0;
	switch(type)
	{
		case P9_SeekWhence_Set:
		{
			pos = offset;
			fid->offset = offset;
		}
		break;
		case P9_SeekWhence_Cur:
		{
			pos = (s64)fid->offset + offset;
			if(pos < 0)
			{
				return -1;
			}
			fid->offset = pos;
		}
		break;
		case P9_SeekWhence_End:
		{
			Dir9P dir = client9p_fid_stat(arena, fid);
			if(dir.name.size == 0)
			{
				return -1;
			}
			pos = (s64)dir.length + offset;
			if(pos < 0)
			{
				return -1;
			}
			fid->offset = pos;
		}
		break;
		default:
		{
			return -1;
		}
	}
	return pos;
}
