// clang-format off
#include "base/inc.h"
#include "9p/inc.h"
#include "base/inc.c"
#include "9p/inc.c"
// clang-format on

global FsContext9P *fs_context = 0;
global WP_Pool *worker_pool = 0;

internal void
fid_release(Server9P *server, ServerFid9P *fid)
{
	if(fid != 0)
	{
		fid->hash_next = server->fid_free_list;
		server->fid_free_list = fid;
	}
}

internal FidAuxiliary9P *
fid_aux_alloc(Server9P *server)
{
	FidAuxiliary9P *aux = server->fid_aux_free_list;
	if(aux != 0)
	{
		server->fid_aux_free_list = aux->next;
	}
	else
	{
		aux = push_array_no_zero(server->arena, FidAuxiliary9P, 1);
	}
	MemoryZeroStruct(aux);
	return aux;
}

internal void
fid_aux_release(Server9P *server, FidAuxiliary9P *aux)
{
	if(aux == 0)
	{
		return;
	}

	if(aux->handle)
	{
		fs9p_close(aux->handle);
		aux->handle = 0;
	}
	if(aux->has_dir_iter)
	{
		fs9p_closedir(&aux->dir_iter);
		aux->has_dir_iter = 0;
	}

	aux->next = server->fid_aux_free_list;
	server->fid_aux_free_list = aux;
}

internal FidAuxiliary9P *
get_fid_aux(Server9P *server, ServerFid9P *fid)
{
	if(fid->auxiliary == 0)
	{
		fid->auxiliary = fid_aux_alloc(server);
	}
	return (FidAuxiliary9P *)fid->auxiliary;
}

internal String8
fid_aux_get_path(FidAuxiliary9P *aux)
{
	return str8(aux->path_buffer, aux->path_len);
}

internal void
fid_aux_set_path(FidAuxiliary9P *aux, String8 path)
{
	Assert(path.size <= sizeof(aux->path_buffer));
	MemoryCopy(aux->path_buffer, path.str, path.size);
	aux->path_len = path.size;
}

////////////////////////////////
//~ 9P Operation Handlers

internal void
srv_version(ServerRequest9P *request)
{
	request->out_msg.max_message_size = request->in_msg.max_message_size;
	request->out_msg.protocol_version = request->in_msg.protocol_version;
	server9p_respond(request, str8_zero());
}

internal void
srv_auth(ServerRequest9P *request)
{
	server9p_respond(request, str8_lit("authentication not required"));
}

internal void
srv_attach(ServerRequest9P *request)
{
	Dir9P root_stat = fs9p_stat(request->scratch.arena, fs_context, str8_zero());
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

internal void
srv_walk(ServerRequest9P *request)
{
	FidAuxiliary9P *from_aux = get_fid_aux(request->server, request->fid);

	if(request->in_msg.walk_name_count == 0)
	{
		FidAuxiliary9P *new_aux = get_fid_aux(request->server, request->new_fid);
		fid_aux_set_path(new_aux, fid_aux_get_path(from_aux));
		request->new_fid->qid = request->fid->qid;
		request->out_msg.walk_qid_count = 0;
		server9p_respond(request, str8_zero());
		return;
	}

	String8 current_path = fid_aux_get_path(from_aux);

	for(u64 i = 0; i < request->in_msg.walk_name_count; i += 1)
	{
		String8 name = request->in_msg.walk_names[i];

		if(str8_match(name, str8_lit("."), 0))
		{
			if(i == 0)
			{
				request->out_msg.walk_qids[i] = request->fid->qid;
			}
			else
			{
				request->out_msg.walk_qids[i] = request->out_msg.walk_qids[i - 1];
			}
			continue;
		}

		PathResolution9P res = fs9p_resolve_path(request->scratch.arena, fs_context, current_path, name);

		if(!res.valid)
		{
			if(i == 0)
			{
				if(request->new_fid != request->fid)
				{
					ServerFid9P *fid = server9p_fid_remove(request->server, request->in_msg.new_fid);
					fid_release(request->server, fid);
				}
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

		Dir9P stat = fs9p_stat(request->scratch.arena, fs_context, res.absolute_path);
		if(stat.name.size == 0)
		{
			if(i == 0)
			{
				if(request->new_fid != request->fid)
				{
					ServerFid9P *fid = server9p_fid_remove(request->server, request->in_msg.new_fid);
					fid_release(request->server, fid);
				}
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

	FidAuxiliary9P *new_aux = get_fid_aux(request->server, request->new_fid);
	fid_aux_set_path(new_aux, current_path);
	request->new_fid->qid = request->out_msg.walk_qids[request->in_msg.walk_name_count - 1];
	request->out_msg.walk_qid_count = request->in_msg.walk_name_count;
	server9p_respond(request, str8_zero());
}

internal void
srv_open(ServerRequest9P *request)
{
	FidAuxiliary9P *aux = get_fid_aux(request->server, request->fid);

	u32 access_mode = request->in_msg.open_mode & 3;
	if(fs_context->readonly && access_mode != P9_OpenFlag_Read)
	{
		server9p_respond(request, str8_lit("read-only filesystem"));
		return;
	}

	FsHandle9P *handle = fs9p_open(request->server->arena, fs_context, fid_aux_get_path(aux), request->in_msg.open_mode);
	if(handle == 0 || (handle->fd < 0 && !handle->is_directory && handle->tmp_node == 0))
	{
		server9p_respond(request, str8_lit("cannot open file"));
		return;
	}

	aux->handle = handle;
	aux->open_mode = request->in_msg.open_mode;

	if(handle->is_directory)
	{
		aux->has_dir_iter = fs9p_opendir(fs_context, fid_aux_get_path(aux), &aux->dir_iter);
	}

	request->out_msg.qid = request->fid->qid;
	request->out_msg.io_unit_size = P9_IOUNIT_DEFAULT;
	server9p_respond(request, str8_zero());
}

internal void
srv_create(ServerRequest9P *request)
{
	FidAuxiliary9P *aux = get_fid_aux(request->server, request->fid);

	if(fs_context->readonly)
	{
		server9p_respond(request, str8_lit("read-only filesystem"));
		return;
	}

	String8 new_path = fs9p_path_join(request->scratch.arena, fid_aux_get_path(aux), request->in_msg.name);

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

	Dir9P stat = fs9p_stat(request->scratch.arena, fs_context, new_path);
	if(stat.name.size == 0)
	{
		server9p_respond(request, str8_lit("failed to create"));
		return;
	}

	fid_aux_set_path(aux, new_path);
	request->fid->qid = stat.qid;

	FsHandle9P *handle = fs9p_open(request->server->arena, fs_context, new_path, request->in_msg.open_mode);
	if(handle)
	{
		aux->handle = handle;
		aux->open_mode = request->in_msg.open_mode;

		if(handle->is_directory)
		{
			aux->has_dir_iter = fs9p_opendir(fs_context, new_path, &aux->dir_iter);
		}
	}

	request->out_msg.qid = stat.qid;
	request->out_msg.io_unit_size = P9_IOUNIT_DEFAULT;
	server9p_respond(request, str8_zero());
}

internal void
srv_read(ServerRequest9P *request)
{
	FidAuxiliary9P *aux = get_fid_aux(request->server, request->fid);

	if(request->fid->qid.type & QidTypeFlag_Directory)
	{
		if(!aux->has_dir_iter)
		{
			aux->has_dir_iter = fs9p_opendir(fs_context, fid_aux_get_path(aux), &aux->dir_iter);
		}

		if(!aux->has_dir_iter)
		{
			server9p_respond(request, str8_lit("cannot read directory"));
			return;
		}

		String8 dir_data = fs9p_readdir(request->scratch.arena, request->server->arena, fs_context, &aux->dir_iter,
		                                &aux->cached_dir_entries, request->in_msg.file_offset, request->in_msg.byte_count);

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
	    fs9p_read(request->scratch.arena, aux->handle, request->in_msg.file_offset, request->in_msg.byte_count);

	request->out_msg.payload_data = data;
	request->out_msg.byte_count = data.size;
	server9p_respond(request, str8_zero());
}

internal void
srv_write(ServerRequest9P *request)
{
	FidAuxiliary9P *aux = get_fid_aux(request->server, request->fid);

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

internal void
srv_stat(ServerRequest9P *request)
{
	FidAuxiliary9P *aux = get_fid_aux(request->server, request->fid);

	Dir9P stat = fs9p_stat(request->scratch.arena, fs_context, fid_aux_get_path(aux));
	if(stat.name.size == 0)
	{
		server9p_respond(request, str8_lit("cannot stat file"));
		return;
	}

	request->out_msg.stat_data = str8_from_dir9p(request->scratch.arena, stat);
	server9p_respond(request, str8_zero());
}

internal void
srv_wstat(ServerRequest9P *request)
{
	if(fs_context->readonly)
	{
		server9p_respond(request, str8_lit("read-only filesystem"));
		return;
	}

	FidAuxiliary9P *aux = get_fid_aux(request->server, request->fid);

	if(request->in_msg.stat_data.size == 0)
	{
		server9p_respond(request, str8_lit("invalid stat data"));
		return;
	}

	Dir9P stat = dir9p_from_str8(request->in_msg.stat_data);

	if(!fs9p_wstat(fs_context, fid_aux_get_path(aux), &stat))
	{
		server9p_respond(request, str8_lit("wstat failed"));
		return;
	}

	if(stat.name.size > 0)
	{
		String8 current_basename = fs9p_basename(request->scratch.arena, fid_aux_get_path(aux));
		if(!str8_match(stat.name, current_basename, 0))
		{
			String8 parent_path = fs9p_dirname(request->scratch.arena, fid_aux_get_path(aux));
			String8 new_path = fs9p_path_join(request->scratch.arena, parent_path, stat.name);
			fid_aux_set_path(aux, new_path);
		}
	}

	server9p_respond(request, str8_zero());
}

internal void
srv_remove(ServerRequest9P *request)
{
	FidAuxiliary9P *aux = get_fid_aux(request->server, request->fid);

	if(fs_context->readonly)
	{
		server9p_respond(request, str8_lit("read-only filesystem"));
		return;
	}

	fs9p_remove(fs_context, fid_aux_get_path(aux));
	fid_aux_release(request->server, aux);
	ServerFid9P *fid = server9p_fid_remove(request->server, request->in_msg.fid);
	fid_release(request->server, fid);
	server9p_respond(request, str8_zero());
}

internal void
srv_clunk(ServerRequest9P *request)
{
	FidAuxiliary9P *aux = get_fid_aux(request->server, request->fid);
	fid_aux_release(request->server, aux);
	ServerFid9P *fid = server9p_fid_remove(request->server, request->in_msg.fid);
	fid_release(request->server, fid);
	server9p_respond(request, str8_zero());
}

////////////////////////////////
//~ Server Loop

internal void
handle_connection(OS_Handle connection_socket)
{
	Temp scratch = scratch_begin(0, 0);
	Arena *connection_arena = arena_alloc();
	Log *log = log_alloc();
	log_select(log);
	log_scope_begin();

	u64 connection_fd = connection_socket.u64[0];
	log_info(str8_lit("9pfs: connection established\n"));

	Server9P *server = server9p_alloc(connection_arena, connection_fd, connection_fd);
	if(server == 0)
	{
		log_error(str8_lit("9pfs: failed to allocate server\n"));
		os_file_close(connection_socket);
		arena_release(connection_arena);
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

	log_info(str8_lit("9pfs: connection closed\n"));

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

	log_release(log);
	arena_release(connection_arena);
	scratch_end(scratch);
}

internal void
handle_connection_task(void *params)
{
	OS_Handle connection = *(OS_Handle *)params;
	handle_connection(connection);
}

////////////////////////////////
//~ Entry Point

internal void
entry_point(CmdLine *cmd_line)
{
	Arena *arena = arena_alloc();

	String8 root_path = cmd_line_string(cmd_line, str8_lit("root"));
	if(root_path.size == 0)
	{
		root_path = str8_lit(".");
	}

	String8 address = str8_zero();
	b32 readonly = cmd_line_has_flag(cmd_line, str8_lit("readonly"));

	u64 worker_count = 0;
	String8 threads_str = cmd_line_string(cmd_line, str8_lit("threads"));
	if(threads_str.size > 0)
	{
		worker_count = u64_from_str8(threads_str, 10);
	}

	if(cmd_line->inputs.node_count > 0)
	{
		address = cmd_line->inputs.first->string;
	}

	if(address.size == 0)
	{
		fprintf(stderr, "usage: 9pfs [options] <address>\n"
		                "options:\n"
		                "  --root=<path>     Root directory to serve (default: current directory)\n"
		                "  --readonly        Serve in read-only mode\n"
		                "  --threads=<n>     Number of worker threads (default: max(4, cores/4))\n"
		                "arguments:\n"
		                "  <address>         Dial string (e.g., tcp!host!port)\n");
		fflush(stderr);
	}
	else
	{
		fs_context = fs9p_context_alloc(arena, root_path, str8_zero(), readonly, StorageBackend9P_Disk);

		OS_Handle listen_socket = dial9p_listen(address, str8_lit("tcp"), str8_lit("9pfs"));
		if(os_handle_match(listen_socket, os_handle_zero()))
		{
			fprintf(stderr, "9pfs: failed to listen on address '%.*s'\n", (int)address.size, address.str);
			fflush(stderr);
		}
		else
		{
			fprintf(stdout, "9pfs: serving '%.*s' on %.*s%s\n", (int)root_path.size, root_path.str, (int)address.size,
			        address.str, readonly ? " (read-only)" : "");
			fflush(stdout);

			if(worker_count == 0)
			{
				u64 logical_cores = os_get_system_info()->logical_processor_count;
				worker_count = Max(4, logical_cores / 4);
			}

			worker_pool = wp_pool_alloc(arena, worker_count);

			fprintf(stdout, "9pfs: launched %lu worker threads\n", (unsigned long)worker_count);
			fflush(stdout);

			for(;;)
			{
				OS_Handle connection_socket = os_socket_accept(listen_socket);
				if(os_handle_match(connection_socket, os_handle_zero()))
				{
					fprintf(stderr, "9pfs: failed to accept connection\n");
					fflush(stderr);
					continue;
				}

				fprintf(stdout, "9pfs: accepted connection\n");
				fflush(stdout);

				wp_submit(worker_pool, handle_connection_task, &connection_socket, sizeof(connection_socket));
			}
		}
	}

	arena_release(arena);
}
