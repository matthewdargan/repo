// Request Management
static ServerRequest9P *
server9p_request_alloc(Server9P *server, u32 tag)
{
	u32 hash = tag % server->max_request_count;
	if(server->request_table[hash] != 0)
	{
		return 0;
	}
	ServerRequest9P *request = push_array(server->arena, ServerRequest9P, 1);
	request->tag = tag;
	request->server = server;
	server->request_table[hash] = request;
	server->request_count += 1;
	return request;
}

static ServerRequest9P *
server9p_request_remove(Server9P *server, u32 tag)
{
	u32 hash = tag % server->max_request_count;
	ServerRequest9P *request = server->request_table[hash];
	if(request != 0 && request->tag == tag)
	{
		server->request_table[hash] = 0;
		server->request_count -= 1;
		return request;
	}
	return 0;
}

// Server Lifecycle
static Server9P *
server9p_alloc(Arena *arena, u64 input_fd, u64 output_fd)
{
	Server9P *server = push_array(arena, Server9P, 1);
	server->arena = arena;
	server->input_fd = input_fd;
	server->output_fd = output_fd;
	server->max_message_size = P9_IOUNIT_DEFAULT + P9_MESSAGE_HEADER_SIZE;
	server->max_fid_count = 256;
	server->max_request_count = 256;
	server->fid_table = push_array(arena, ServerFid9P *, server->max_fid_count);
	server->request_table = push_array(arena, ServerRequest9P *, server->max_request_count);
	server->next_tag = 1;
	server->read_buffer = push_array(arena, u8, server->max_message_size);
	server->write_buffer = push_array(arena, u8, server->max_message_size);
	return server;
}

// Request Handling
static ServerRequest9P *
server9p_get_request(Server9P *server)
{
	String8 msg = read_9p_msg(server->arena, server->input_fd);
	if(msg.size == 0)
	{
		return 0;
	}
	Message9P f = msg9p_from_str8(msg);
	if(f.type == 0)
	{
		return 0;
	}
	ServerRequest9P *request = server9p_request_alloc(server, f.tag);
	if(request == 0)
	{
		request = push_array(server->arena, ServerRequest9P, 1);
		request->tag = f.tag;
		request->in_msg = f;
		request->error = str8_lit("duplicate tag");
		request->buffer = msg.str;
		request->responded = 0;
		request->server = server;
		return request;
	}

	request->server = server;
	request->responded = 0;
	request->buffer = msg.str;
	request->in_msg = f;
	request->out_msg = msg9p_zero();
	switch(f.type)
	{
		case Msg9P_Tattach:
		{
			request->fid = server9p_fid_alloc(server, f.fid);
			if(request->fid == 0)
			{
				request->error = str8_lit("duplicate fid");
			}
			else
			{
				request->fid->user_id = str8_copy(server->arena, f.user_name);
			}
		}
		break;
		case Msg9P_Twalk:
		{
			request->fid = server9p_fid_lookup(server, f.fid);
			if(request->fid == 0)
			{
				request->error = str8_lit("unknown fid");
			}
			else if(f.fid != f.new_fid)
			{
				request->new_fid = server9p_fid_alloc(server, f.new_fid);
				if(request->new_fid == 0)
				{
					request->error = str8_lit("duplicate fid");
				}
				else
				{
					request->new_fid->user_id = str8_copy(server->arena, request->fid->user_id);
				}
			}
			else
			{
				request->new_fid = request->fid;
			}
		}
		break;
		case Msg9P_Topen:
		case Msg9P_Tcreate:
		case Msg9P_Tread:
		case Msg9P_Twrite:
		case Msg9P_Tstat:
		case Msg9P_Twstat:
		case Msg9P_Tclunk:
		case Msg9P_Tremove:
		{
			request->fid = server9p_fid_lookup(server, f.fid);
			if(request->fid == 0)
			{
				request->error = str8_lit("unknown fid");
			}
		}
		break;
		default:
			break;
	}
	return request;
}

static b32
server9p_respond(ServerRequest9P *request, String8 err)
{
	Server9P *server = request->server;
	if(request->responded != 0)
	{
		return 0;
	}
	request->responded = 1;
	request->error = err;
	request->out_msg.tag = request->in_msg.tag;
	request->out_msg.type = request->in_msg.type + 1;
	if(err.size > 0)
	{
		request->out_msg.error_message = err;
		request->out_msg.type = Msg9P_Rerror;
	}

	String8 buf = str8_from_msg9p(server->arena, request->out_msg);
	if(buf.size == 0)
	{
		return 0;
	}
	server9p_request_remove(server, request->in_msg.tag);

	u64 total_num_bytes_to_write = buf.size;
	u64 total_num_bytes_written = 0;
	u64 total_num_bytes_left_to_write = total_num_bytes_to_write;
	for(; total_num_bytes_left_to_write > 0;)
	{
		ssize_t write_result = write(server->output_fd, buf.str + total_num_bytes_written, total_num_bytes_left_to_write);
		if(write_result >= 0)
		{
			total_num_bytes_written += write_result;
			total_num_bytes_left_to_write -= write_result;
		}
		else if(errno != EINTR)
		{
			return 0;
		}
	}
	return total_num_bytes_written == total_num_bytes_to_write;
}

// Fid Management
static ServerFid9P *
server9p_fid_alloc(Server9P *server, u32 fid)
{
	u32 hash = fid % server->max_fid_count;
	if(server->fid_table[hash] != 0)
	{
		return 0;
	}
	ServerFid9P *f = push_array(server->arena, ServerFid9P, 1);
	f->fid = fid;
	f->open_mode = P9_OPEN_MODE_NONE;
	f->server = server;
	server->fid_table[hash] = f;
	server->fid_count += 1;
	return f;
}

static ServerFid9P *
server9p_fid_lookup(Server9P *server, u32 fid)
{
	u32 hash = fid % server->max_fid_count;
	ServerFid9P *f = server->fid_table[hash];
	if(f != 0 && f->fid == fid)
	{
		return f;
	}
	return 0;
}

static ServerFid9P *
server9p_fid_remove(Server9P *server, u32 fid)
{
	u32 hash = fid % server->max_fid_count;
	ServerFid9P *f = server->fid_table[hash];
	if(f != 0 && f->fid == fid)
	{
		server->fid_table[hash] = 0;
		server->fid_count -= 1;
		return f;
	}
	return 0;
}
