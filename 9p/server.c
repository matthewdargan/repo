// Message Error Handling
static void
msg9p_set_error(Message9P *f, String8 err)
{
	f->error_message = err;
	f->type = Msg9P_Rerror;
}

// Message Size Management
static void
server9p_change_message_size(Server9P *server, u32 max_message_size)
{
	if(server->read_buffer != 0 && server->write_buffer != 0 && server->max_message_size == max_message_size)
	{
		return;
	}
	server->max_message_size = max_message_size;
	server->read_buffer = push_array(server->arena, u8, max_message_size);
	server->write_buffer = push_array(server->arena, u8, max_message_size);
}

// Request Reading
static ServerRequest9P *
server9p_get_request(Server9P *server)
{
	String8 msg = read_9p_msg(server->arena, server->input_fd);
	if(msg.size <= 0)
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
		request->error = Eduptag;
		request->buffer = msg.str;
		request->responded = 0;
		request->server = server;
		// TODO: logging
		return request;
	}
	request->server = server;
	request->responded = 0;
	request->buffer = msg.str;
	request->in_msg = f;
	request->out_msg = (Message9P){0};
	// TODO: logging
	return request;
}

// Message Handlers
static void
server9p_handle_version(Server9P *server, ServerRequest9P *request)
{
	if(!str8_match(request->in_msg.protocol_version, version_9p, 0))
	{
		request->out_msg.protocol_version = str8_lit("unknown");
		server9p_respond(request, str8_zero());
		return;
	}
	request->out_msg.protocol_version = version_9p;
	request->out_msg.max_message_size = request->in_msg.max_message_size;
	server9p_respond(request, str8_zero());
}

static void
server9p_reply_version(ServerRequest9P *request, String8 err)
{
	if(err.size > 0)
	{
		return;
	}
	server9p_change_message_size(request->server, request->out_msg.max_message_size);
}

static void
server9p_handle_auth(Server9P *server, ServerRequest9P *request)
{
	request->auth_fid = server9p_fid_alloc(server, request->in_msg.auth_fid);
	if(request->auth_fid == 0)
	{
		server9p_respond(request, Edupfid);
		return;
	}
	if(server->auth != 0)
	{
		server->auth(request);
	}
	else
	{
		String8 err = str8f(server->arena, "authentication not required");
		server9p_respond(request, err);
	}
}

static void
server9p_reply_auth(ServerRequest9P *request, String8 err)
{
	if(err.size > 0 && request->auth_fid != 0)
	{
		server9p_fid_close(server9p_fid_remove(request->server, request->auth_fid->fid));
	}
}

static void
server9p_handle_attach(Server9P *server, ServerRequest9P *request)
{
	request->fid = server9p_fid_alloc(server, request->in_msg.fid);
	if(request->fid == 0)
	{
		server9p_respond(request, Edupfid);
		return;
	}
	request->auth_fid = 0;
	if(request->in_msg.auth_fid != P9_FID_NONE)
	{
		request->auth_fid = server9p_fid_lookup(server, request->in_msg.auth_fid);
		if(request->auth_fid == 0)
		{
			server9p_respond(request, Eunknownfid);
			return;
		}
	}
	request->fid->user_id = str8_copy(server->arena, request->in_msg.user_name);
	if(server->attach != 0)
	{
		server->attach(request);
	}
	else
	{
		server9p_respond(request, str8_zero());
	}
}

static void
server9p_reply_attach(ServerRequest9P *request, String8 err)
{
	if(err.size > 0 && request->fid != 0)
	{
		server9p_fid_close(server9p_fid_remove(request->server, request->fid->fid));
	}
}

static void
server9p_handle_flush(Server9P *server, ServerRequest9P *request)
{
	request->old_request = server9p_request_lookup(server, request->in_msg.cancel_tag);
	if(request->old_request == 0 || request->old_request == request)
	{
		server9p_respond(request, str8_zero());
	}
	else if(server->flush != 0)
	{
		server->flush(request);
	}
	else
	{
		server9p_respond(request, str8_zero());
	}
}

static b32
server9p_reply_flush(ServerRequest9P *request, String8 err)
{
	if(err.size > 0)
	{
		return 0;
	}
	ServerRequest9P *old_request = request->old_request;
	if(old_request != 0)
	{
		if(old_request->responded == 0)
		{
			old_request->flush = push_array(request->server->arena, ServerRequest9P *, old_request->flush_count + 1);
			old_request->flush[old_request->flush_count] = request;
			old_request->flush_count += 1;
			return 1;
		}
		server9p_request_close(old_request);
	}
	request->old_request = 0;
	return 0;
}

static void
server9p_handle_walk(Server9P *server, ServerRequest9P *request)
{
	request->fid = server9p_fid_lookup(server, request->in_msg.fid);
	if(request->fid == 0)
	{
		server9p_respond(request, Eunknownfid);
		return;
	}
	if(request->fid->open_mode != ~0U)
	{
		server9p_respond(request, str8_lit("cannot clone open fid"));
		return;
	}
	if(request->in_msg.walk_name_count && !(request->fid->qid.type & QidTypeFlag_Directory))
	{
		server9p_respond(request, Ewalknodir);
		return;
	}
	if(request->in_msg.fid != request->in_msg.new_fid)
	{
		request->new_fid = server9p_fid_alloc(server, request->in_msg.new_fid);
		if(request->new_fid == 0)
		{
			server9p_respond(request, Edupfid);
			return;
		}
		request->new_fid->user_id = str8_copy(server->arena, request->fid->user_id);
	}
	else
	{
		request->new_fid = request->fid;
	}
	if(server->walk != 0)
	{
		server->walk(request);
	}
	else
	{
		server9p_respond(request, str8_lit("no walk function"));
	}
}

static void
server9p_reply_walk(ServerRequest9P *request, String8 err)
{
	if(err.size > 0 || request->out_msg.walk_qid_count < request->in_msg.walk_name_count)
	{
		if(request->in_msg.fid != request->in_msg.new_fid && request->new_fid != 0)
		{
			server9p_fid_close(server9p_fid_remove(request->server, request->new_fid->fid));
		}
		if(request->out_msg.walk_qid_count == 0)
		{
			if(err.size == 0 && request->in_msg.walk_name_count != 0)
			{
				request->error = Enotfound;
			}
		}
		else
		{
			request->error = str8_zero();
		}
	}
	else
	{
		if(request->out_msg.walk_qid_count == 0)
		{
			request->new_fid->qid = request->fid->qid;
		}
		else
		{
			request->new_fid->qid = request->out_msg.walk_qids[request->out_msg.walk_qid_count - 1];
		}
	}
}

static void
server9p_handle_open(Server9P *server, ServerRequest9P *request)
{
	request->fid = server9p_fid_lookup(server, request->in_msg.fid);
	if(request->fid == 0)
	{
		server9p_respond(request, Eunknownfid);
		return;
	}
	if(request->fid->open_mode != ~0U)
	{
		server9p_respond(request, Ebotch);
		return;
	}
	if((request->fid->qid.type & QidTypeFlag_Directory) && (request->in_msg.open_mode & ~0x10) != P9_OpenFlag_Read)
	{
		server9p_respond(request, Eisdir);
		return;
	}
	request->out_msg.qid = request->fid->qid;
	u32 p = 0;
	switch(request->in_msg.open_mode & 3)
	{
		case P9_OpenFlag_Read:
		{
			p = P9_AccessFlag_Read;
		}
		break;
		case P9_OpenFlag_Write:
		{
			p = P9_AccessFlag_Write;
		}
		break;
		case P9_OpenFlag_ReadWrite:
		{
			p = P9_AccessFlag_Read | P9_AccessFlag_Write;
		}
		break;
		case P9_OpenFlag_Execute:
		{
			p = P9_AccessFlag_Execute;
		}
		break;
		default:
		{
			server9p_respond(request, Ebotch);
			return;
		}
	}
	if(request->in_msg.open_mode & P9_OpenFlag_Truncate)
	{
		p |= P9_AccessFlag_Write;
	}
	if((request->fid->qid.type & QidTypeFlag_Directory) && p != P9_AccessFlag_Read)
	{
		server9p_respond(request, Eperm);
		return;
	}
	if(server->open != 0)
	{
		server->open(request);
	}
	else
	{
		server9p_respond(request, str8_zero());
	}
}

static void
server9p_reply_open(ServerRequest9P *request, String8 err)
{
	if(err.size > 0)
	{
		return;
	}
	request->fid->open_mode = request->in_msg.open_mode;
	request->fid->qid = request->out_msg.qid;
	if(request->out_msg.qid.type & QidTypeFlag_Directory)
	{
		request->fid->offset = 0;
	}
}

static void
server9p_handle_create(Server9P *server, ServerRequest9P *request)
{
	request->fid = server9p_fid_lookup(server, request->in_msg.fid);
	if(request->fid == 0)
	{
		server9p_respond(request, Eunknownfid);
	}
	else if(request->fid->open_mode != ~0U)
	{
		server9p_respond(request, Ebotch);
	}
	else if(!(request->fid->qid.type & QidTypeFlag_Directory))
	{
		server9p_respond(request, Ecreatenondir);
	}
	else if(server->create != 0)
	{
		server->create(request);
	}
	else
	{
		server9p_respond(request, Enocreate);
	}
}

static void
server9p_reply_create(ServerRequest9P *request, String8 err)
{
	if(err.size > 0)
	{
		return;
	}
	request->fid->open_mode = request->in_msg.open_mode;
	request->fid->qid = request->out_msg.qid;
}

static void
server9p_handle_read(Server9P *server, ServerRequest9P *request)
{
	request->fid = server9p_fid_lookup(server, request->in_msg.fid);
	if(request->fid == 0)
	{
		server9p_respond(request, Eunknownfid);
		return;
	}
	if((s32)request->in_msg.byte_count < 0)
	{
		server9p_respond(request, Ebotch);
		return;
	}
	if(request->in_msg.file_offset < 0 ||
	   ((request->fid->qid.type & QidTypeFlag_Directory) && request->in_msg.file_offset != 0 &&
	    request->in_msg.file_offset != request->fid->offset))
	{
		server9p_respond(request, Ebadoffset);
		return;
	}
	if(request->in_msg.byte_count > server->max_message_size - P9_MESSAGE_HEADER_SIZE)
	{
		request->in_msg.byte_count = server->max_message_size - P9_MESSAGE_HEADER_SIZE;
	}
	request->read_buffer = push_array(server->arena, u8, request->in_msg.byte_count);
	request->out_msg.payload_data = str8(request->read_buffer, 0);
	u32 open_mode = request->fid->open_mode & 3;
	if(open_mode != P9_OpenFlag_Read && open_mode != P9_OpenFlag_ReadWrite && open_mode != P9_OpenFlag_Execute)
	{
		server9p_respond(request, Ebotch);
		return;
	}
	if(server->read != 0)
	{
		server->read(request);
	}
	else
	{
		server9p_respond(request, str8_lit("no server->read"));
	}
}

static void
server9p_reply_read(ServerRequest9P *request, String8 err)
{
	if(err.size == 0 && (request->fid->qid.type & QidTypeFlag_Directory))
	{
		request->fid->offset = request->in_msg.file_offset + request->out_msg.byte_count;
	}
}

static void
server9p_handle_write(Server9P *server, ServerRequest9P *request)
{
	request->fid = server9p_fid_lookup(server, request->in_msg.fid);
	if(request->fid == 0)
	{
		server9p_respond(request, Eunknownfid);
		return;
	}
	if((s32)request->in_msg.byte_count < 0)
	{
		server9p_respond(request, Ebotch);
		return;
	}
	if(request->in_msg.file_offset < 0)
	{
		server9p_respond(request, Ebotch);
		return;
	}
	if(request->in_msg.byte_count > server->max_message_size - P9_MESSAGE_HEADER_SIZE)
	{
		request->in_msg.byte_count = server->max_message_size - P9_MESSAGE_HEADER_SIZE;
	}
	u32 open_mode = request->fid->open_mode & 3;
	if(open_mode != P9_OpenFlag_Write && open_mode != P9_OpenFlag_ReadWrite)
	{
		String8 err = str8f(server->arena, "write on fid with open mode 0x%x", request->fid->open_mode);
		server9p_respond(request, err);
		return;
	}
	if(server->write != 0)
	{
		server->write(request);
	}
	else
	{
		server9p_respond(request, str8_lit("no server->write"));
	}
}

static void
server9p_reply_write(ServerRequest9P *request, String8 err)
{
	if(err.size > 0)
	{
		return;
	}
}

static void
server9p_handle_clunk(Server9P *server, ServerRequest9P *request)
{
	request->fid = server9p_fid_remove(server, request->in_msg.fid);
	if(request->fid == 0)
	{
		server9p_respond(request, Eunknownfid);
	}
	else
	{
		server9p_respond(request, str8_zero());
	}
}

static void
server9p_reply_clunk(ServerRequest9P *request, String8 err)
{
	(void)request;
	(void)err;
}

static void
server9p_handle_remove(Server9P *server, ServerRequest9P *request)
{
	request->fid = server9p_fid_remove(server, request->in_msg.fid);
	if(request->fid == 0)
	{
		server9p_respond(request, Eunknownfid);
		return;
	}
	if(server->remove != 0)
	{
		server->remove(request);
	}
	else
	{
		server9p_respond(request, Enoremove);
	}
}

static void
server9p_reply_remove(ServerRequest9P *request, String8 err)
{
	(void)request;
	(void)err;
}

static void
server9p_handle_stat(Server9P *server, ServerRequest9P *request)
{
	request->fid = server9p_fid_lookup(server, request->in_msg.fid);
	if(request->fid == 0)
	{
		server9p_respond(request, Eunknownfid);
		return;
	}
	if(server->stat != 0)
	{
		server->stat(request);
	}
	else
	{
		server9p_respond(request, Enostat);
	}
}

static void
server9p_reply_stat(ServerRequest9P *request, String8 err)
{
	(void)request;
	(void)err;
}

static void
server9p_handle_wstat(Server9P *server, ServerRequest9P *request)
{
	request->fid = server9p_fid_lookup(server, request->in_msg.fid);
	if(request->fid == 0)
	{
		server9p_respond(request, Eunknownfid);
		return;
	}
	if(server->wstat == 0)
	{
		server9p_respond(request, Enowstat);
		return;
	}
	server->wstat(request);
}

static void
server9p_reply_wstat(ServerRequest9P *request, String8 err)
{
	(void)request;
	(void)err;
}

// Server Lifecycle
static Server9P *
server9p_alloc(Arena *arena, u64 input_fd, u64 output_fd)
{
	Server9P *server = push_array(arena, Server9P, 1);
	server->arena = arena;
	server->input_fd = input_fd;
	server->output_fd = output_fd;
	server->max_message_size = 8192 + P9_MESSAGE_HEADER_SIZE;
	server->max_fid_count = 256;
	server->max_request_count = 256;
	server->fid_table = push_array(arena, ServerFid9P *, server->max_fid_count);
	server->request_table = push_array(arena, ServerRequest9P *, server->max_request_count);
	server->next_tag = 1;
	server9p_change_message_size(server, server->max_message_size);
	return server;
}

static void
server9p_free(Server9P *server)
{
	for(u64 i = 0; i < server->max_fid_count; i += 1)
	{
		if(server->fid_table[i] != 0)
		{
			server9p_fid_close(server->fid_table[i]);
		}
	}
	for(u64 i = 0; i < server->max_request_count; i += 1)
	{
		if(server->request_table[i] != 0)
		{
			server9p_request_close(server->request_table[i]);
		}
	}
}

static void
server9p_run(Server9P *server)
{
	if(server->start != 0)
	{
		server->start(server);
	}
	for(;;)
	{
		ServerRequest9P *request = server9p_get_request(server);
		if(request == 0)
		{
			break;
		}
		if(request->error.size > 0)
		{
			server9p_respond(request, request->error);
			continue;
		}
		switch(request->in_msg.type)
		{
			case Msg9P_Tversion:
			{
				server9p_handle_version(server, request);
			}
			break;
			case Msg9P_Tauth:
			{
				server9p_handle_auth(server, request);
			}
			break;
			case Msg9P_Tattach:
			{
				server9p_handle_attach(server, request);
			}
			break;
			case Msg9P_Tflush:
			{
				server9p_handle_flush(server, request);
			}
			break;
			case Msg9P_Twalk:
			{
				server9p_handle_walk(server, request);
			}
			break;
			case Msg9P_Topen:
			{
				server9p_handle_open(server, request);
			}
			break;
			case Msg9P_Tcreate:
			{
				server9p_handle_create(server, request);
			}
			break;
			case Msg9P_Tread:
			{
				server9p_handle_read(server, request);
			}
			break;
			case Msg9P_Twrite:
			{
				server9p_handle_write(server, request);
			}
			break;
			case Msg9P_Tclunk:
			{
				server9p_handle_clunk(server, request);
			}
			break;
			case Msg9P_Tremove:
			{
				server9p_handle_remove(server, request);
			}
			break;
			case Msg9P_Tstat:
			{
				server9p_handle_stat(server, request);
			}
			break;
			case Msg9P_Twstat:
			{
				server9p_handle_wstat(server, request);
			}
			break;
			default:
			{
				server9p_respond(request, str8_lit("unknown message"));
			}
			break;
		}
	}
	if(server->end != 0)
	{
		server->end(server);
	}
}

// Request Handling
static void
server9p_respond(ServerRequest9P *request, String8 err)
{
	Server9P *server = request->server;
	if(request->responded != 0)
	{
		goto free;
	}
	request->responded = 1;
	request->error = err;
	switch(request->in_msg.type)
	{
		case Msg9P_Tflush:
		{
			if(server9p_reply_flush(request, err))
			{
				return;
			}
		}
		break;
		case Msg9P_Tversion:
		{
			server9p_reply_version(request, err);
		}
		break;
		case Msg9P_Tauth:
		{
			server9p_reply_auth(request, err);
		}
		break;
		case Msg9P_Tattach:
		{
			server9p_reply_attach(request, err);
		}
		break;
		case Msg9P_Twalk:
		{
			server9p_reply_walk(request, err);
		}
		break;
		case Msg9P_Topen:
		{
			server9p_reply_open(request, err);
		}
		break;
		case Msg9P_Tcreate:
		{
			server9p_reply_create(request, err);
		}
		break;
		case Msg9P_Tread:
		{
			server9p_reply_read(request, err);
		}
		break;
		case Msg9P_Twrite:
		{
			server9p_reply_write(request, err);
		}
		break;
		case Msg9P_Tclunk:
		{
			server9p_reply_clunk(request, err);
		}
		break;
		case Msg9P_Tremove:
		{
			server9p_reply_remove(request, err);
		}
		break;
		case Msg9P_Tstat:
		{
			server9p_reply_stat(request, err);
		}
		break;
		case Msg9P_Twstat:
		{
			server9p_reply_wstat(request, err);
		}
		break;
		default:
			break;
	}
	request->out_msg.tag = request->in_msg.tag;
	request->out_msg.type = request->in_msg.type + 1;
	if(request->error.size > 0)
	{
		msg9p_set_error(&request->out_msg, request->error);
	}
	// TODO: logging
	String8 buf = str8_from_msg9p(server->arena, request->out_msg);
	if(buf.size <= 0)
	{
		// TODO: log error
		return;
	}
	server9p_request_remove(server, request->in_msg.tag);
	u64 n = write(server->output_fd, buf.str, buf.size);
	if(n != buf.size)
	{
		// TODO: log error
		return;
	}

free:
	for(u64 i = 0; i < request->flush_count; i += 1)
	{
		request->flush[i]->old_request = 0;
		server9p_respond(request->flush[i], str8_zero());
	}
	server9p_request_close(request);
}

// Fid Management
static ServerFid9P *
server9p_fid_alloc(Server9P *server, u32 fid)
{
	u32 hash = fid % server->max_fid_count;
	if(server->fid_table[hash])
	{
		return 0;
	}
	ServerFid9P *f = push_array(server->arena, ServerFid9P, 1);
	f->fid = fid;
	f->open_mode = ~0U;
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
	if(f && f->fid == fid)
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
	if(f && f->fid == fid)
	{
		server->fid_table[hash] = 0;
		server->fid_count -= 1;
		return f;
	}
	return 0;
}

static void
server9p_fid_close(ServerFid9P *fid)
{
	if(fid->server->destroy_fid != 0)
	{
		fid->server->destroy_fid(fid);
	}
}

// Request Management
static ServerRequest9P *
server9p_request_alloc(Server9P *server, u32 tag)
{
	u32 hash = tag % server->max_request_count;
	if(server->request_table[hash])
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
server9p_request_lookup(Server9P *server, u32 tag)
{
	u32 hash = tag % server->max_request_count;
	ServerRequest9P *request = server->request_table[hash];
	if(request && request->tag == tag)
	{
		return request;
	}
	return 0;
}

static ServerRequest9P *
server9p_request_remove(Server9P *server, u32 tag)
{
	u32 hash = tag % server->max_request_count;
	ServerRequest9P *request = server->request_table[hash];
	if(request && request->tag == tag)
	{
		server->request_table[hash] = 0;
		server->request_count -= 1;
		return request;
	}
	return 0;
}

static void
server9p_request_close(ServerRequest9P *request)
{
	if(request->server->destroy_request != 0)
	{
		request->server->destroy_request(request);
	}
}
