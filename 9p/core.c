// 9P Protocol Encoding/Decoding Helpers
static u8 *
encode_str8(u8 *ptr, String8 s)
{
	write_u16(ptr, from_le_u16(s.size));
	ptr += 2;
	if(s.size > 0)
	{
		MemoryCopy(ptr, s.str, s.size);
		ptr += s.size;
	}
	return ptr;
}

static u8 *
encode_qid(u8 *ptr, Qid qid)
{
	*ptr = (u8)qid.type;
	ptr += 1;
	write_u32(ptr, from_le_u32(qid.version));
	ptr += 4;
	write_u64(ptr, from_le_u64(qid.path));
	ptr += 8;
	return ptr;
}

static u8 *
decode_str8(u8 *ptr, u8 *end, String8 *s)
{
	if(ptr + 2 > end)
	{
		return 0;
	}
	u32 size = (u32)from_le_u16(read_u16(ptr));
	ptr += 2;
	if(ptr + size > end)
	{
		return 0;
	}
	s->size = size;
	if(size > 0)
	{
		s->str = ptr;
		ptr += size;
	}
	else
	{
		s->str = 0;
	}
	return ptr;
}

static u8 *
decode_qid(u8 *ptr, u8 *end, Qid *qid)
{
	if(ptr + 13 > end)
	{
		return 0;
	}
	qid->type = (u32)*ptr;
	ptr += 1;
	qid->version = (u32)from_le_u32(read_u32(ptr));
	ptr += 4;
	qid->path = (u64)from_le_u64(read_u64(ptr));
	ptr += 8;
	return ptr;
}

// 9P Message Encoding/Decoding
static u32
msg9p_size(Message9P msg)
{
	u32 result = 4 + 1 + 2;
	switch(msg.type)
	{
		case Msg9P_Tversion:
		case Msg9P_Rversion:
		{
			result += 4;
			result += 2 + msg.protocol_version.size;
		}
		break;
		case Msg9P_Tauth:
		{
			result += 4;
			result += 2 + msg.user_name.size;
			result += 2 + msg.attach_path.size;
		}
		break;
		case Msg9P_Rauth:
		{
			result += 13;
		}
		break;
		case Msg9P_Rerror:
		{
			result += 2 + msg.error_message.size;
		}
		break;
		case Msg9P_Tflush:
		{
			result += 2;
		}
		break;
		case Msg9P_Rflush:
			break;
		case Msg9P_Tattach:
		{
			result += 4;
			result += 4;
			result += 2 + msg.user_name.size;
			result += 2 + msg.attach_path.size;
		}
		break;
		case Msg9P_Rattach:
		{
			result += 13;
		}
		break;
		case Msg9P_Twalk:
		{
			result += 4;
			result += 4;
			result += 2;
			for(u64 i = 0; i < msg.walk_name_count; i += 1)
			{
				result += 2 + msg.walk_names[i].size;
			}
		}
		break;
		case Msg9P_Rwalk:
		{
			result += 2;
			result += msg.walk_qid_count * 13;
		}
		break;
		case Msg9P_Topen:
		{
			result += 4;
			result += 1;
		}
		break;
		case Msg9P_Ropen:
		case Msg9P_Rcreate:
		{
			result += 13;
			result += 4;
		}
		break;
		case Msg9P_Tcreate:
		{
			result += 4;
			result += 2 + msg.name.size;
			result += 4;
			result += 1;
		}
		break;
		case Msg9P_Tread:
		{
			result += 4;
			result += 8;
			result += 4;
		}
		break;
		case Msg9P_Rread:
		{
			result += 4;
			result += msg.payload_data.size;
		}
		break;
		case Msg9P_Twrite:
		{
			result += 4;
			result += 8;
			result += 4;
			result += msg.payload_data.size;
		}
		break;
		case Msg9P_Rwrite:
		{
			result += 4;
		}
		break;
		case Msg9P_Tclunk:
		case Msg9P_Tremove:
		{
			result += 4;
		}
		break;
		case Msg9P_Rclunk:
		case Msg9P_Rremove:
			break;
		case Msg9P_Tstat:
		{
			result += 4;
		}
		break;
		case Msg9P_Rstat:
		{
			result += 2 + msg.stat_data.size;
		}
		break;
		case Msg9P_Twstat:
		{
			result += 4;
			result += 2 + msg.stat_data.size;
		}
		break;
		case Msg9P_Rwstat:
			break;
		default:
		{
			return 0;
		}
	}
	return result;
}

static String8
str8_from_msg9p(Arena *arena, Message9P msg)
{
	u32 msg_size = msg9p_size(msg);
	if(msg_size == 0)
	{
		return str8_zero();
	}
	String8 result = {0};
	result.str = push_array_no_zero(arena, u8, msg_size);
	result.size = msg_size;
	u8 *ptr = result.str;
	write_u32(ptr, from_le_u32(result.size));
	ptr += 4;
	*ptr = (u8)msg.type;
	ptr += 1;
	write_u16(ptr, from_le_u16(msg.tag));
	ptr += 2;
	switch(msg.type)
	{
		case Msg9P_Tversion:
		case Msg9P_Rversion:
		{
			write_u32(ptr, from_le_u32(msg.max_message_size));
			ptr += 4;
			ptr = encode_str8(ptr, msg.protocol_version);
		}
		break;
		case Msg9P_Tauth:
		{
			write_u32(ptr, from_le_u32(msg.auth_fid));
			ptr += 4;
			ptr = encode_str8(ptr, msg.user_name);
			ptr = encode_str8(ptr, msg.attach_path);
		}
		break;
		case Msg9P_Rauth:
		{
			ptr = encode_qid(ptr, msg.auth_qid);
		}
		break;
		case Msg9P_Rerror:
		{
			ptr = encode_str8(ptr, msg.error_message);
		}
		break;
		case Msg9P_Tflush:
		{
			write_u16(ptr, from_le_u16(msg.cancel_tag));
			ptr += 2;
		}
		break;
		case Msg9P_Rflush:
			break;
		case Msg9P_Tattach:
		{
			write_u32(ptr, from_le_u32(msg.fid));
			ptr += 4;
			write_u32(ptr, from_le_u32(msg.auth_fid));
			ptr += 4;
			ptr = encode_str8(ptr, msg.user_name);
			ptr = encode_str8(ptr, msg.attach_path);
		}
		break;
		case Msg9P_Rattach:
		{
			ptr = encode_qid(ptr, msg.qid);
		}
		break;
		case Msg9P_Twalk:
		{
			write_u32(ptr, from_le_u32(msg.fid));
			ptr += 4;
			write_u32(ptr, from_le_u32(msg.new_fid));
			ptr += 4;
			write_u16(ptr, from_le_u16(msg.walk_name_count));
			ptr += 2;
			if(msg.walk_name_count > MAX_WALK_ELEM_COUNT)
			{
				return str8_zero();
			}
			for(u64 i = 0; i < msg.walk_name_count; i += 1)
			{
				ptr = encode_str8(ptr, msg.walk_names[i]);
			}
		}
		break;
		case Msg9P_Rwalk:
		{
			write_u16(ptr, from_le_u16(msg.walk_qid_count));
			ptr += 2;
			if(msg.walk_qid_count > MAX_WALK_ELEM_COUNT)
			{
				return str8_zero();
			}
			for(u64 i = 0; i < msg.walk_qid_count; i += 1)
			{
				ptr = encode_qid(ptr, msg.walk_qids[i]);
			}
		}
		break;
		case Msg9P_Topen:
		{
			write_u32(ptr, from_le_u32(msg.fid));
			ptr += 4;
			*ptr = (u8)msg.open_mode;
			ptr += 1;
		}
		break;
		case Msg9P_Ropen:
		case Msg9P_Rcreate:
		{
			ptr = encode_qid(ptr, msg.qid);
			write_u32(ptr, from_le_u32(msg.io_unit_size));
			ptr += 4;
		}
		break;
		case Msg9P_Tcreate:
		{
			write_u32(ptr, from_le_u32(msg.fid));
			ptr += 4;
			ptr = encode_str8(ptr, msg.name);
			write_u32(ptr, from_le_u32(msg.permissions));
			ptr += 4;
			*ptr = (u8)msg.open_mode;
			ptr += 1;
		}
		break;
		case Msg9P_Tread:
		{
			write_u32(ptr, from_le_u32(msg.fid));
			ptr += 4;
			write_u64(ptr, from_le_u64(msg.file_offset));
			ptr += 8;
			write_u32(ptr, from_le_u32(msg.byte_count));
			ptr += 4;
		}
		break;
		case Msg9P_Rread:
		{
			write_u32(ptr, from_le_u32(msg.payload_data.size));
			ptr += 4;
			if(msg.payload_data.size > 0)
			{
				MemoryCopy(ptr, msg.payload_data.str, msg.payload_data.size);
				ptr += msg.payload_data.size;
			}
		}
		break;
		case Msg9P_Twrite:
		{
			write_u32(ptr, from_le_u32(msg.fid));
			ptr += 4;
			write_u64(ptr, from_le_u64(msg.file_offset));
			ptr += 8;
			write_u32(ptr, from_le_u32(msg.payload_data.size));
			ptr += 4;
			if(msg.payload_data.size > 0)
			{
				MemoryCopy(ptr, msg.payload_data.str, msg.payload_data.size);
				ptr += msg.payload_data.size;
			}
		}
		break;
		case Msg9P_Rwrite:
		{
			write_u32(ptr, from_le_u32(msg.byte_count));
			ptr += 4;
		}
		break;
		case Msg9P_Tclunk:
		case Msg9P_Tremove:
		{
			write_u32(ptr, from_le_u32(msg.fid));
			ptr += 4;
		}
		break;
		case Msg9P_Rclunk:
		case Msg9P_Rremove:
			break;
		case Msg9P_Tstat:
		{
			write_u32(ptr, from_le_u32(msg.fid));
			ptr += 4;
		}
		break;
		case Msg9P_Rstat:
		{
			write_u16(ptr, from_le_u16(msg.stat_data.size));
			ptr += 2;
			if(msg.stat_data.size > 0)
			{
				MemoryCopy(ptr, msg.stat_data.str, msg.stat_data.size);
				ptr += msg.stat_data.size;
			}
		}
		break;
		case Msg9P_Twstat:
		{
			write_u32(ptr, from_le_u32(msg.fid));
			ptr += 4;
			write_u16(ptr, from_le_u16(msg.stat_data.size));
			ptr += 2;
			if(msg.stat_data.size > 0)
			{
				MemoryCopy(ptr, msg.stat_data.str, msg.stat_data.size);
				ptr += msg.stat_data.size;
			}
		}
		break;
		case Msg9P_Rwstat:
			break;
		default:
		{
			return str8_zero();
		}
	}
	if(result.size != (u64)(ptr - result.str))
	{
		return str8_zero();
	}
	return result;
}

static Message9P
msg9p_from_str8(String8 data)
{
	Message9P msg = {0};
	Message9P err_msg = {0};
	if(data.size < 7)
	{
		return err_msg;
	}
	u8 *ptr = data.str;
	u8 *end = data.str + data.size;
	u32 size = (u32)from_le_u32(read_u32(ptr));
	ptr += 4;
	if(size != data.size)
	{
		return err_msg;
	}
	msg.type = (u32)*ptr;
	ptr += 1;
	msg.tag = (u32)from_le_u16(read_u16(ptr));
	ptr += 2;
	switch(msg.type)
	{
		case Msg9P_Tversion:
		case Msg9P_Rversion:
		{
			if(ptr + 4 > end)
			{
				return err_msg;
			}
			msg.max_message_size = (u32)from_le_u32(read_u32(ptr));
			ptr += 4;
			ptr = decode_str8(ptr, end, &msg.protocol_version);
			if(ptr == 0)
			{
				return err_msg;
			}
		}
		break;
		case Msg9P_Tauth:
		{
			if(ptr + 4 > end)
			{
				return err_msg;
			}
			msg.auth_fid = (u32)from_le_u32(read_u32(ptr));
			ptr += 4;
			ptr = decode_str8(ptr, end, &msg.user_name);
			if(ptr == 0)
			{
				return err_msg;
			}
			ptr = decode_str8(ptr, end, &msg.attach_path);
			if(ptr == 0)
			{
				return err_msg;
			}
		}
		break;
		case Msg9P_Rauth:
		{
			ptr = decode_qid(ptr, end, &msg.auth_qid);
			if(ptr == 0)
			{
				return err_msg;
			}
		}
		break;
		case Msg9P_Rerror:
		{
			ptr = decode_str8(ptr, end, &msg.error_message);
			if(ptr == 0)
			{
				return err_msg;
			}
		}
		break;
		case Msg9P_Tflush:
		{
			if(ptr + 2 > end)
			{
				return err_msg;
			}
			msg.cancel_tag = (u32)from_le_u16(read_u16(ptr));
			ptr += 2;
		}
		break;
		case Msg9P_Rflush:
			break;
		case Msg9P_Tattach:
		{
			if(ptr + 8 > end)
			{
				return err_msg;
			}
			msg.fid = (u32)from_le_u32(read_u32(ptr));
			ptr += 4;
			msg.auth_fid = (u32)from_le_u32(read_u32(ptr));
			ptr += 4;
			ptr = decode_str8(ptr, end, &msg.user_name);
			if(ptr == 0)
			{
				return err_msg;
			}
			ptr = decode_str8(ptr, end, &msg.attach_path);
			if(ptr == 0)
			{
				return err_msg;
			}
		}
		break;
		case Msg9P_Rattach:
		{
			ptr = decode_qid(ptr, end, &msg.qid);
			if(ptr == 0)
			{
				return err_msg;
			}
		}
		break;
		case Msg9P_Twalk:
		{
			if(ptr + 10 > end)
			{
				return err_msg;
			}
			msg.fid = (u32)from_le_u32(read_u32(ptr));
			ptr += 4;
			msg.new_fid = (u32)from_le_u32(read_u32(ptr));
			ptr += 4;
			msg.walk_name_count = (u32)from_le_u16(read_u16(ptr));
			ptr += 2;
			if(msg.walk_name_count > MAX_WALK_ELEM_COUNT)
			{
				return err_msg;
			}
			for(u64 i = 0; i < msg.walk_name_count; i += 1)
			{
				ptr = decode_str8(ptr, end, &msg.walk_names[i]);
				if(ptr == 0)
				{
					return err_msg;
				}
			}
		}
		break;
		case Msg9P_Rwalk:
		{
			if(ptr + 2 > end)
			{
				return err_msg;
			}
			msg.walk_qid_count = (u32)from_le_u16(read_u16(ptr));
			ptr += 2;
			if(msg.walk_qid_count > MAX_WALK_ELEM_COUNT)
			{
				return err_msg;
			}
			for(u64 i = 0; i < msg.walk_qid_count; i += 1)
			{
				ptr = decode_qid(ptr, end, &msg.walk_qids[i]);
				if(ptr == 0)
				{
					return err_msg;
				}
			}
		}
		break;
		case Msg9P_Topen:
		{
			if(ptr + 5 > end)
			{
				return err_msg;
			}
			msg.fid = (u32)from_le_u32(read_u32(ptr));
			ptr += 4;
			msg.open_mode = (u32)*ptr;
			ptr += 1;
		}
		break;
		case Msg9P_Ropen:
		case Msg9P_Rcreate:
		{
			ptr = decode_qid(ptr, end, &msg.qid);
			if(ptr == 0)
			{
				return err_msg;
			}
			if(ptr + 4 > end)
			{
				return err_msg;
			}
			msg.io_unit_size = (u32)from_le_u32(read_u32(ptr));
			ptr += 4;
		}
		break;
		case Msg9P_Tcreate:
		{
			if(ptr + 4 > end)
			{
				return err_msg;
			}
			msg.fid = (u32)from_le_u32(read_u32(ptr));
			ptr += 4;
			ptr = decode_str8(ptr, end, &msg.name);
			if(ptr == 0)
			{
				return err_msg;
			}
			if(ptr + 5 > end)
			{
				return err_msg;
			}
			msg.permissions = (u32)from_le_u32(read_u32(ptr));
			ptr += 4;
			msg.open_mode = (u32)*ptr;
			ptr += 1;
		}
		break;
		case Msg9P_Tread:
		{
			if(ptr + 16 > end)
			{
				return err_msg;
			}
			msg.fid = (u32)from_le_u32(read_u32(ptr));
			ptr += 4;
			msg.file_offset = (u64)from_le_u64(read_u64(ptr));
			ptr += 8;
			msg.byte_count = (u32)from_le_u32(read_u32(ptr));
			ptr += 4;
		}
		break;
		case Msg9P_Rread:
		{
			if(ptr + 4 > end)
			{
				return err_msg;
			}
			msg.payload_data.size = (u32)from_le_u32(read_u32(ptr));
			ptr += 4;
			if(ptr + msg.payload_data.size > end)
			{
				return err_msg;
			}
			if(msg.payload_data.size > 0)
			{
				msg.payload_data.str = ptr;
				ptr += msg.payload_data.size;
			}
			else
			{
				msg.payload_data.str = 0;
			}
		}
		break;
		case Msg9P_Twrite:
		{
			if(ptr + 16 > end)
			{
				return err_msg;
			}
			msg.fid = (u32)from_le_u32(read_u32(ptr));
			ptr += 4;
			msg.file_offset = (u64)from_le_u64(read_u64(ptr));
			ptr += 8;
			msg.payload_data.size = (u32)from_le_u32(read_u32(ptr));
			ptr += 4;
			if(ptr + msg.payload_data.size > end)
			{
				return err_msg;
			}
			if(msg.payload_data.size > 0)
			{
				msg.payload_data.str = ptr;
				ptr += msg.payload_data.size;
			}
			else
			{
				msg.payload_data.str = 0;
			}
		}
		break;
		case Msg9P_Rwrite:
		{
			if(ptr + 4 > end)
			{
				return err_msg;
			}
			msg.byte_count = (u32)from_le_u32(read_u32(ptr));
			ptr += 4;
		}
		break;
		case Msg9P_Tclunk:
		case Msg9P_Tremove:
		{
			if(ptr + 4 > end)
			{
				return err_msg;
			}
			msg.fid = (u32)from_le_u32(read_u32(ptr));
			ptr += 4;
		}
		break;
		case Msg9P_Rclunk:
		case Msg9P_Rremove:
			break;
		case Msg9P_Tstat:
		{
			if(ptr + 4 > end)
			{
				return err_msg;
			}
			msg.fid = (u32)from_le_u32(read_u32(ptr));
			ptr += 4;
		}
		break;
		case Msg9P_Rstat:
		{
			if(ptr + 2 > end)
			{
				return err_msg;
			}
			msg.stat_data.size = (u32)from_le_u16(read_u16(ptr));
			ptr += 2;
			if(ptr + msg.stat_data.size > end)
			{
				return err_msg;
			}
			if(msg.stat_data.size > 0)
			{
				msg.stat_data.str = ptr;
				ptr += msg.stat_data.size;
			}
			else
			{
				msg.stat_data.str = 0;
			}
		}
		break;
		case Msg9P_Twstat:
		{
			if(ptr + 6 > end)
			{
				return err_msg;
			}
			msg.fid = (u32)from_le_u32(read_u32(ptr));
			ptr += 4;
			msg.stat_data.size = (u32)from_le_u16(read_u16(ptr));
			ptr += 2;
			if(ptr + msg.stat_data.size > end)
			{
				return err_msg;
			}
			if(msg.stat_data.size > 0)
			{
				msg.stat_data.str = ptr;
				ptr += msg.stat_data.size;
			}
			else
			{
				msg.stat_data.str = 0;
			}
		}
		break;
		case Msg9P_Rwstat:
			break;
		default:
		{
			return err_msg;
		}
	}
	if(ptr != end)
	{
		return err_msg;
	}
	return msg;
}

// 9P Directory Encoding/Decoding
static u32
dir_size(Dir dir)
{
	u32 result = STAT_DATA_FIXED_SIZE;
	result += 2 + dir.name.size;
	result += 2 + dir.user_id.size;
	result += 2 + dir.group_id.size;
	result += 2 + dir.modify_user_id.size;
	return result;
}

static String8
str8_from_dir(Arena *arena, Dir dir)
{
	u32 entry_size = dir_size(dir);
	if(entry_size == 0)
	{
		return str8_zero();
	}
	String8 msg = {0};
	msg.str = push_array_no_zero(arena, u8, entry_size);
	msg.size = entry_size;
	u8 *ptr = msg.str;
	write_u16(ptr, from_le_u16(msg.size - 2));
	ptr += 2;
	write_u16(ptr, from_le_u16(dir.server_type));
	ptr += 2;
	write_u32(ptr, from_le_u32(dir.server_dev));
	ptr += 4;
	*ptr = (u8)dir.qid.type;
	ptr += 1;
	write_u32(ptr, from_le_u32(dir.qid.version));
	ptr += 4;
	write_u64(ptr, from_le_u64(dir.qid.path));
	ptr += 8;
	write_u32(ptr, from_le_u32(dir.mode));
	ptr += 4;
	write_u32(ptr, from_le_u32(dir.access_time));
	ptr += 4;
	write_u32(ptr, from_le_u32(dir.modify_time));
	ptr += 4;
	write_u64(ptr, from_le_u64(dir.length));
	ptr += 8;
	ptr = encode_str8(ptr, dir.name);
	ptr = encode_str8(ptr, dir.user_id);
	ptr = encode_str8(ptr, dir.group_id);
	ptr = encode_str8(ptr, dir.modify_user_id);
	if(msg.size != (u64)(ptr - msg.str))
	{
		return str8_zero();
	}
	return msg;
}

static Dir
dir_from_str8(String8 data)
{
	Dir dir = {0};
	Dir err_dir = {0};
	if(data.size < STAT_DATA_FIXED_SIZE)
	{
		return err_dir;
	}
	u8 *ptr = data.str;
	u8 *end = data.str + data.size;
	ptr += 2;
	if(ptr + 39 > end)
	{
		return err_dir;
	}
	dir.server_type = (u32)from_le_u16(read_u16(ptr));
	ptr += 2;
	dir.server_dev = (u32)from_le_u32(read_u32(ptr));
	ptr += 4;
	dir.qid.type = (u32)*ptr;
	ptr += 1;
	dir.qid.version = (u32)from_le_u32(read_u32(ptr));
	ptr += 4;
	dir.qid.path = (u64)from_le_u64(read_u64(ptr));
	ptr += 8;
	dir.mode = (u32)from_le_u32(read_u32(ptr));
	ptr += 4;
	dir.access_time = (u32)from_le_u32(read_u32(ptr));
	ptr += 4;
	dir.modify_time = (u32)from_le_u32(read_u32(ptr));
	ptr += 4;
	dir.length = (u64)from_le_u64(read_u64(ptr));
	ptr += 8;
	ptr = decode_str8(ptr, end, &dir.name);
	if(ptr == 0)
	{
		return err_dir;
	}
	ptr = decode_str8(ptr, end, &dir.user_id);
	if(ptr == 0)
	{
		return err_dir;
	}
	ptr = decode_str8(ptr, end, &dir.group_id);
	if(ptr == 0)
	{
		return err_dir;
	}
	ptr = decode_str8(ptr, end, &dir.modify_user_id);
	if(ptr == 0)
	{
		return err_dir;
	}
	if(ptr != end)
	{
		return err_dir;
	}
	return dir;
}

// 9P Directory List Operations
static void
dir_list_push(Arena *arena, DirList *list, Dir dir)
{
	DirNode *node = push_array_no_zero(arena, DirNode, 1);
	node->dir = dir;
	if(list->first == 0)
	{
		list->first = node;
		list->last = node;
	}
	else
	{
		list->last->next = node;
		list->last = node;
	}
	list->count += 1;
}

// 9P Message I/O
static String8
read_9p_msg(Arena *arena, u64 fd)
{
	u8 lenbuf[4];
	u32 total_bytes_read = 0;
	u32 total_bytes_left_to_read = 4;
	for(; total_bytes_left_to_read > 0;)
	{
		ssize_t n = read(fd, lenbuf + total_bytes_read, total_bytes_left_to_read);
		if(n <= 0)
		{
			return str8_zero();
		}
		total_bytes_read += n;
		total_bytes_left_to_read -= n;
	}
	u32 msg_size = (u32)from_le_u32(read_u32(lenbuf));
	String8 msg = {0};
	msg.str = push_array_no_zero(arena, u8, msg_size);
	msg.size = msg_size;
	MemoryCopy(msg.str, lenbuf, sizeof lenbuf);
	total_bytes_read = 0;
	total_bytes_left_to_read = msg.size - 4;
	for(; total_bytes_left_to_read > 0;)
	{
		ssize_t n = read(fd, msg.str + 4 + total_bytes_read, total_bytes_left_to_read);
		if(n <= 0)
		{
			return str8_zero();
		}
		total_bytes_read += n;
		total_bytes_left_to_read -= n;
	}
	return msg;
}
