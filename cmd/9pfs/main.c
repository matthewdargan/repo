// clang-format off
#include "base/inc.h"
#include "9p/inc.h"
#include "base/inc.c"
#include "9p/inc.c"
// clang-format on

static FsContext9P *fs_context = 0;

static FidAuxiliary9P *
get_fid_aux(Arena *arena, ServerFid9P *fid)
{
	if(fid->auxiliary == 0)
	{
		FidAuxiliary9P *aux = push_array(arena, FidAuxiliary9P, 1);
		fid->auxiliary = aux;
	}
	return (FidAuxiliary9P *)fid->auxiliary;
}

// 9P Operation Handlers
static void
srv_version(ServerRequest9P *request)
{
	request->out_msg.max_message_size = request->in_msg.max_message_size;
	request->out_msg.protocol_version = request->in_msg.protocol_version;
	server9p_respond(request, str8_zero());
}

static void
srv_auth(ServerRequest9P *request)
{
	server9p_respond(request, str8_lit("authentication not required"));
}

static void
srv_attach(ServerRequest9P *request)
{
	Dir9P root_stat = fs9p_stat(request->server->arena, fs_context, str8_zero());
	if(root_stat.name.size == 0)
	{
		request->fid->qid.path = 0;
		request->fid->qid.version = 0;
		request->fid->qid.type = QidTypeFlag_Directory;
	}
	else
	{
		request->fid->qid = root_stat.qid;
	}

	request->out_msg.qid = request->fid->qid;
	server9p_respond(request, str8_zero());
}

static void
srv_walk(ServerRequest9P *request)
{
	FidAuxiliary9P *from_aux = get_fid_aux(request->server->arena, request->fid);

	if(request->in_msg.walk_name_count == 0)
	{
		FidAuxiliary9P *new_aux = get_fid_aux(request->server->arena, request->new_fid);
		new_aux->path = str8_copy(request->server->arena, from_aux->path);
		request->new_fid->qid = request->fid->qid;
		request->out_msg.walk_qid_count = 0;
		server9p_respond(request, str8_zero());
		return;
	}

	String8 current_path = from_aux->path;

	for(u64 i = 0; i < request->in_msg.walk_name_count; i += 1)
	{
		String8 name = request->in_msg.walk_names[i];

		if(str8_match(name, str8_lit("."), 0))
		{
			request->out_msg.walk_qids[i] = request->fid->qid;
			continue;
		}

		PathResolution9P res = fs9p_resolve_path(request->server->arena, fs_context, current_path, name);

		if(!res.valid)
		{
			if(i == 0)
			{
				server9p_respond(request, res.error);
				return;
			}
			else
			{
				request->out_msg.walk_qid_count = i;
				server9p_respond(request, str8_zero());
				return;
			}
		}

		Dir9P stat = fs9p_stat(request->server->arena, fs_context, res.absolute_path);
		if(stat.name.size == 0)
		{
			if(i == 0)
			{
				server9p_respond(request, str8_lit("file not found"));
				return;
			}
			else
			{
				request->out_msg.walk_qid_count = i;
				server9p_respond(request, str8_zero());
				return;
			}
		}

		request->out_msg.walk_qids[i] = stat.qid;
		current_path = res.absolute_path;
	}

	FidAuxiliary9P *new_aux = get_fid_aux(request->server->arena, request->new_fid);
	new_aux->path = current_path;
	request->new_fid->qid = request->out_msg.walk_qids[request->in_msg.walk_name_count - 1];
	request->out_msg.walk_qid_count = request->in_msg.walk_name_count;
	server9p_respond(request, str8_zero());
}

static void
srv_open(ServerRequest9P *request)
{
	FidAuxiliary9P *aux = get_fid_aux(request->server->arena, request->fid);

	u32 access_mode = request->in_msg.open_mode & 3;
	if(fs_context->readonly && access_mode != P9_OpenFlag_Read)
	{
		server9p_respond(request, str8_lit("read-only filesystem"));
		return;
	}

	FsHandle9P *handle = fs9p_open(request->server->arena, fs_context, aux->path, request->in_msg.open_mode);
	if(handle == 0 || (handle->fd < 0 && !handle->is_directory && handle->tmp_node == 0))
	{
		server9p_respond(request, str8_lit("cannot open file"));
		return;
	}

	aux->handle = handle;
	aux->open_mode = request->in_msg.open_mode;

	if(handle->is_directory)
	{
		aux->dir_iter = fs9p_opendir(request->server->arena, fs_context, aux->path);
	}

	request->out_msg.qid = request->fid->qid;
	request->out_msg.io_unit_size = P9_IOUNIT_DEFAULT;
	server9p_respond(request, str8_zero());
}

static void
srv_create(ServerRequest9P *request)
{
	FidAuxiliary9P *aux = get_fid_aux(request->server->arena, request->fid);

	if(fs_context->readonly)
	{
		server9p_respond(request, str8_lit("read-only filesystem"));
		return;
	}

	String8 new_path = fs9p_path_join(request->server->arena, aux->path, request->in_msg.name);

	if(!fs9p_path_is_safe(request->in_msg.name))
	{
		server9p_respond(request, str8_lit("unsafe filename"));
		return;
	}

	if(!fs9p_create(fs_context, new_path, request->in_msg.permissions, request->in_msg.open_mode))
	{
		server9p_respond(request, str8_lit("file already exists"));
		return;
	}

	Dir9P stat = fs9p_stat(request->server->arena, fs_context, new_path);
	if(stat.name.size == 0)
	{
		server9p_respond(request, str8_lit("failed to create"));
		return;
	}

	aux->path = new_path;
	request->fid->qid = stat.qid;

	FsHandle9P *handle = fs9p_open(request->server->arena, fs_context, new_path, request->in_msg.open_mode);
	if(handle)
	{
		aux->handle = handle;
		aux->open_mode = request->in_msg.open_mode;

		if(handle->is_directory)
		{
			aux->dir_iter = fs9p_opendir(request->server->arena, fs_context, new_path);
		}
	}

	request->out_msg.qid = stat.qid;
	request->out_msg.io_unit_size = P9_IOUNIT_DEFAULT;
	server9p_respond(request, str8_zero());
}

static void
srv_read(ServerRequest9P *request)
{
	FidAuxiliary9P *aux = get_fid_aux(request->server->arena, request->fid);

	if(request->fid->qid.type & QidTypeFlag_Directory)
	{
		if(aux->dir_iter == 0)
		{
			aux->dir_iter = fs9p_opendir(request->server->arena, fs_context, aux->path);
		}

		if(aux->dir_iter == 0)
		{
			server9p_respond(request, str8_lit("cannot read directory"));
			return;
		}

		String8 dir_data = fs9p_readdir(request->server->arena, fs_context, aux->dir_iter, request->in_msg.file_offset,
		                                request->in_msg.byte_count);

		request->out_msg.payload_data = dir_data;
		request->out_msg.byte_count = dir_data.size;
		server9p_respond(request, str8_zero());
		return;
	}

	if(aux->handle == 0 || (aux->handle->fd < 0 && aux->handle->tmp_node == 0))
	{
		server9p_respond(request, str8_lit("file not open"));
		return;
	}

	String8 data =
	    fs9p_read(request->server->arena, aux->handle, request->in_msg.file_offset, request->in_msg.byte_count);

	request->out_msg.payload_data = data;
	request->out_msg.byte_count = data.size;
	server9p_respond(request, str8_zero());
}

static void
srv_write(ServerRequest9P *request)
{
	FidAuxiliary9P *aux = get_fid_aux(request->server->arena, request->fid);

	if(fs_context->readonly)
	{
		server9p_respond(request, str8_lit("read-only filesystem"));
		return;
	}

	if(aux->handle == 0 || (aux->handle->fd < 0 && aux->handle->tmp_node == 0))
	{
		server9p_respond(request, str8_lit("file not open"));
		return;
	}

	u64 bytes_written = fs9p_write(aux->handle, request->in_msg.file_offset, request->in_msg.payload_data);

	request->out_msg.byte_count = bytes_written;
	server9p_respond(request, str8_zero());
}

static void
srv_stat(ServerRequest9P *request)
{
	FidAuxiliary9P *aux = get_fid_aux(request->server->arena, request->fid);

	Dir9P stat = fs9p_stat(request->server->arena, fs_context, aux->path);
	if(stat.name.size == 0)
	{
		server9p_respond(request, str8_lit("cannot stat file"));
		return;
	}

	request->out_msg.stat_data = str8_from_dir9p(request->server->arena, stat);
	server9p_respond(request, str8_zero());
}

static void
srv_wstat(ServerRequest9P *request)
{
	if(fs_context->readonly)
	{
		server9p_respond(request, str8_lit("read-only filesystem"));
		return;
	}

	FidAuxiliary9P *aux = get_fid_aux(request->server->arena, request->fid);

	if(request->in_msg.stat_data.size == 0)
	{
		server9p_respond(request, str8_lit("invalid stat data"));
		return;
	}

	Dir9P stat = dir9p_from_str8(request->in_msg.stat_data);
	if(stat.name.size == 0 && request->in_msg.stat_data.size > 0)
	{
		server9p_respond(request, str8_lit("invalid stat format"));
		return;
	}

	fs9p_wstat(fs_context, aux->path, &stat);

	if(stat.name.size > 0)
	{
		String8 current_basename = fs9p_basename(request->server->arena, aux->path);
		if(!str8_match(stat.name, current_basename, 0))
		{
			String8 parent_path = fs9p_dirname(request->server->arena, aux->path);
			aux->path = fs9p_path_join(request->server->arena, parent_path, stat.name);
		}
	}

	server9p_respond(request, str8_zero());
}

static void
srv_remove(ServerRequest9P *request)
{
	FidAuxiliary9P *aux = get_fid_aux(request->server->arena, request->fid);

	if(fs_context->readonly)
	{
		server9p_respond(request, str8_lit("read-only filesystem"));
		return;
	}

	if(aux->handle)
	{
		fs9p_close(aux->handle);
		aux->handle = 0;
	}
	if(aux->dir_iter)
	{
		fs9p_closedir(aux->dir_iter);
		aux->dir_iter = 0;
	}

	fs9p_remove(fs_context, aux->path);
	server9p_fid_remove(request->server, request->in_msg.fid);
	server9p_respond(request, str8_zero());
}

static void
srv_clunk(ServerRequest9P *request)
{
	FidAuxiliary9P *aux = get_fid_aux(request->server->arena, request->fid);

	if(aux->handle)
	{
		fs9p_close(aux->handle);
		aux->handle = 0;
	}
	if(aux->dir_iter)
	{
		fs9p_closedir(aux->dir_iter);
		aux->dir_iter = 0;
	}

	server9p_fid_remove(request->server, request->in_msg.fid);
	server9p_respond(request, str8_zero());
}

// Server Loop
static void
handle_connection(OS_Handle connection_socket)
{
	Temp scratch = scratch_begin(0, 0);
	DateTime now = os_now_universal_time();
	String8 timestamp = str8_from_datetime(scratch.arena, now);
	u64 connection_fd = connection_socket.u64[0];
	log_infof("[%S] 9pfs: connection established\n", timestamp);

	Server9P *server = server9p_alloc(scratch.arena, connection_fd, connection_fd);
	if(server == 0)
	{
		log_error(str8_lit("9pfs: failed to allocate server\n"));
		os_file_close(connection_socket);
		return;
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
				srv_version(request);
				break;
			case Msg9P_Tauth:
				srv_auth(request);
				break;
			case Msg9P_Tattach:
				srv_attach(request);
				break;
			case Msg9P_Twalk:
				srv_walk(request);
				break;
			case Msg9P_Topen:
				srv_open(request);
				break;
			case Msg9P_Tcreate:
				srv_create(request);
				break;
			case Msg9P_Tread:
				srv_read(request);
				break;
			case Msg9P_Twrite:
				srv_write(request);
				break;
			case Msg9P_Tstat:
				srv_stat(request);
				break;
			case Msg9P_Twstat:
				srv_wstat(request);
				break;
			case Msg9P_Tremove:
				srv_remove(request);
				break;
			case Msg9P_Tclunk:
				srv_clunk(request);
				break;
			default:
				server9p_respond(request, str8_lit("unsupported operation"));
				break;
		}
	}

	os_file_close(connection_socket);

	DateTime end_time = os_now_universal_time();
	String8 end_timestamp = str8_from_datetime(scratch.arena, end_time);
	log_infof("[%S] 9pfs: connection closed\n", end_timestamp);

	scratch_end(scratch);
}

// Entry Point
static void
entry_point(CmdLine *cmd_line)
{
	Temp scratch = scratch_begin(0, 0);
	Log *log = log_alloc();
	log_select(log);
	log_scope_begin();
	Arena *arena = arena_alloc();

	String8 root_path = str8_lit(".");
	String8 address = str8_zero();
	b32 readonly = 0;

	for(CmdLineOpt *opt = cmd_line->options.first; opt != 0; opt = opt->next)
	{
		String8 option = opt->string;
		if(str8_match(option, str8_lit("readonly"), 0) || str8_match(option, str8_lit("r"), 0))
		{
			readonly = 1;
		}
		else if(str8_match(option, str8_lit("root"), 0))
		{
			if(opt->value_string.size > 0)
			{
				root_path = opt->value_string;
			}
		}
	}

	if(cmd_line->inputs.node_count > 0)
	{
		address = cmd_line->inputs.first->string;
	}

	if(address.size == 0)
	{
		log_error(str8_lit("usage: 9pfs [options] <address>\n"
		                   "options:\n"
		                   "  --root=<path>     Root directory to serve (default: current directory)\n"
		                   "  --readonly        Serve in read-only mode\n"
		                   "arguments:\n"
		                   "  <address>         Dial string (e.g., tcp!host!port)\n"));
	}
	else
	{
		fs_context = fs9p_context_alloc(arena, root_path, str8_zero(), readonly);

		OS_Handle listen_socket = dial9p_listen(address, str8_lit("tcp"), str8_lit("9pfs"));
		if(os_handle_match(listen_socket, os_handle_zero()))
		{
			log_errorf("9pfs: failed to listen on address '%S'\n", address);
		}
		else
		{
			log_infof("9pfs: serving '%S' on %S%S\n", root_path, address, readonly ? str8_lit(" (read-only)") : str8_zero());

			for(;;)
			{
				OS_Handle connection_socket = os_socket_accept(listen_socket);
				if(os_handle_match(connection_socket, os_handle_zero()))
				{
					log_error(str8_lit("9pfs: failed to accept connection\n"));
					continue;
				}

				DateTime accept_time = os_now_universal_time();
				String8 accept_timestamp = str8_from_datetime(scratch.arena, accept_time);
				log_infof("[%S] 9pfs: accepted connection\n", accept_timestamp);

				handle_connection(connection_socket);

				LogScopeResult conn_result = log_scope_end(scratch.arena);
				log_scope_begin();
				if(conn_result.strings[LogMsgKind_Info].size > 0)
				{
					fwrite(conn_result.strings[LogMsgKind_Info].str, 1, conn_result.strings[LogMsgKind_Info].size, stdout);
					fflush(stdout);
				}
				if(conn_result.strings[LogMsgKind_Error].size > 0)
				{
					fwrite(conn_result.strings[LogMsgKind_Error].str, 1, conn_result.strings[LogMsgKind_Error].size, stderr);
					fflush(stderr);
				}
			}
		}
	}

	arena_release(arena);

	LogScopeResult result = log_scope_end(scratch.arena);
	if(result.strings[LogMsgKind_Info].size > 0)
	{
		fwrite(result.strings[LogMsgKind_Info].str, 1, result.strings[LogMsgKind_Info].size, stdout);
		fflush(stdout);
	}
	if(result.strings[LogMsgKind_Error].size > 0)
	{
		fwrite(result.strings[LogMsgKind_Error].str, 1, result.strings[LogMsgKind_Error].size, stderr);
		fflush(stderr);
	}
	scratch_end(scratch);
}
