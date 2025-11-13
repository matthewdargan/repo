// clang-format off
#include "base/inc.h"
#include "9p/inc.h"
#include "base/inc.c"
#include "9p/inc.c"
// clang-format on

typedef struct Ramfile Ramfile;
struct Ramfile
{
	String8 data;
	Arena *arena;
};

typedef struct Ramentry Ramentry;
struct Ramentry
{
	String8 name;
	u64 qid_path;
	Ramfile *file;
	Ramentry *next;
};

static Ramentry *filelist = 0;
static u64 next_qid_path = 1;

static Ramentry *
find_file(String8 name)
{
	for(Ramentry *entry = filelist; entry != 0; entry = entry->next)
	{
		if(str8_match(entry->name, name, 0))
		{
			return entry;
		}
	}
	return 0;
}

static Ramentry *
add_file(Arena *arena, String8 name)
{
	Ramentry *entry = push_array(arena, Ramentry, 1);
	entry->name = str8_copy(arena, name);
	entry->qid_path = next_qid_path;
	next_qid_path += 1;
	entry->file = 0;
	entry->next = filelist;
	filelist = entry;
	return entry;
}

static void
ramfs_read(ServerRequest9P *request)
{
	Ramentry *entry = request->fid->auxiliary;

	if(entry == 0 && (request->fid->qid.type & QidTypeFlag_Directory))
	{
		String8 dirdata = str8_zero();
		u64 pos = 0;
		time_t now = os_now_unix();
		for(Ramentry *file = filelist; file != 0; file = file->next)
		{
			if(pos >= request->in_msg.file_offset)
			{
				Dir9P dir = dir9p_zero();
				dir.qid = (Qid){.path = file->qid_path, .version = 0, .type = QidTypeFlag_File};
				dir.mode = 0644;
				dir.name = file->name;
				dir.user_id = str8_lit("ramfs");
				dir.group_id = str8_lit("ramfs");
				dir.modify_user_id = str8_lit("ramfs");
				dir.access_time = now;
				dir.modify_time = now;
				dir.length = file->file ? file->file->data.size : 0;

				String8 entry = str8_from_dir9p(request->server->arena, dir);
				u64 entrylen = entry.size;
				if(dirdata.size + entrylen > request->in_msg.byte_count)
				{
					break;
				}
				if(dirdata.size == 0)
				{
					dirdata = entry;
				}
				else
				{
					u8 *newdata = push_array(request->server->arena, u8, dirdata.size + entrylen);
					MemoryCopy(newdata, dirdata.str, dirdata.size);
					MemoryCopy(newdata + dirdata.size, entry.str, entrylen);
					dirdata.str = newdata;
					dirdata.size = dirdata.size + entrylen;
				}
			}
			pos += 1;
		}
		request->out_msg.payload_data = dirdata;
		request->out_msg.byte_count = dirdata.size;
		server9p_respond(request, str8_zero());
		return;
	}

	if(entry == 0 || entry->file == 0)
	{
		request->out_msg.payload_data = str8_zero();
		request->out_msg.byte_count = 0;
		server9p_respond(request, str8_zero());
		return;
	}

	Ramfile *ram_file = entry->file;
	u64 offset = request->in_msg.file_offset;
	u64 count = request->in_msg.byte_count;
	if(offset >= ram_file->data.size)
	{
		request->out_msg.payload_data = str8_zero();
		request->out_msg.byte_count = 0;
		server9p_respond(request, str8_zero());
		return;
	}
	if(offset + count > ram_file->data.size)
	{
		count = ram_file->data.size - offset;
	}

	request->read_buffer = push_array(request->server->arena, u8, count);
	MemoryCopy(request->read_buffer, ram_file->data.str + offset, count);
	request->out_msg.payload_data = str8(request->read_buffer, count);
	request->out_msg.byte_count = count;
	server9p_respond(request, str8_zero());
}

static void
ramfs_write(ServerRequest9P *request)
{
	Ramentry *entry = request->fid->auxiliary;
	if(entry == 0 || entry->file == 0)
	{
		server9p_respond(request, str8_lit("no file data"));
		return;
	}

	Ramfile *ram_file = entry->file;
	u64 offset = request->in_msg.file_offset;
	u64 count = request->in_msg.payload_data.size;
	if(offset + count > ram_file->data.size)
	{
		u64 newsize = offset + count;
		u8 *newdata = push_array(ram_file->arena, u8, newsize);
		if(ram_file->data.size > 0)
		{
			MemoryCopy(newdata, ram_file->data.str, ram_file->data.size);
		}
		ram_file->data.str = newdata;
		ram_file->data.size = newsize;
	}

	MemoryCopy(ram_file->data.str + offset, request->in_msg.payload_data.str, count);
	request->out_msg.byte_count = count;
	server9p_respond(request, str8_zero());
}

static void
ramfs_create(ServerRequest9P *request)
{
	Ramentry *entry = find_file(request->in_msg.name);
	if(entry != 0)
	{
		server9p_respond(request, str8_lit("file exists"));
		return;
	}

	Ramfile *ram_file = push_array(request->server->arena, Ramfile, 1);
	ram_file->arena = request->server->arena;
	ram_file->data = str8_zero();

	entry = add_file(request->server->arena, request->in_msg.name);
	entry->file = ram_file;

	request->fid->auxiliary = entry;
	request->fid->qid.path = entry->qid_path;
	request->fid->qid.version = 0;
	request->fid->qid.type = QidTypeFlag_File;
	request->out_msg.qid = request->fid->qid;
	server9p_respond(request, str8_zero());
}

static void
ramfs_version(ServerRequest9P *request)
{
	request->out_msg.max_message_size = request->in_msg.max_message_size;
	request->out_msg.protocol_version = request->in_msg.protocol_version;
	server9p_respond(request, str8_zero());
}

static void
ramfs_attach(ServerRequest9P *request)
{
	request->fid->qid.path = 0;
	request->fid->qid.version = 0;
	request->fid->qid.type = QidTypeFlag_Directory;
	request->out_msg.qid = request->fid->qid;
	server9p_respond(request, str8_zero());
}

static void
ramfs_walk(ServerRequest9P *request)
{
	if(request->in_msg.walk_name_count == 0)
	{
		request->new_fid->qid = request->fid->qid;
		request->out_msg.walk_qid_count = 0;
		server9p_respond(request, str8_zero());
		return;
	}

	for(u64 i = 0; i < request->in_msg.walk_name_count; i += 1)
	{
		if(!(request->fid->qid.type & QidTypeFlag_Directory))
		{
			server9p_respond(request, str8_lit("not a directory"));
			return;
		}

		String8 name = request->in_msg.walk_names[i];
		Ramentry *entry = find_file(name);
		if(entry == 0)
		{
			server9p_respond(request, str8_lit("file not found"));
			return;
		}

		request->new_fid->qid.path = entry->qid_path;
		request->new_fid->qid.version = 0;
		request->new_fid->qid.type = QidTypeFlag_File;
		request->new_fid->auxiliary = entry;
		request->out_msg.walk_qids[i] = request->new_fid->qid;
	}

	request->out_msg.walk_qid_count = request->in_msg.walk_name_count;
	server9p_respond(request, str8_zero());
}

static void
ramfs_stat(ServerRequest9P *request)
{
	Ramentry *entry = request->fid->auxiliary;
	time_t now = os_now_unix();
	Dir9P dir = dir9p_zero();

	if(entry == 0)
	{
		dir.qid = request->fid->qid;
		dir.mode = P9_ModeFlag_Directory | 0755;
		dir.name = str8_copy(request->server->arena, str8_lit("."));
		dir.user_id = str8_copy(request->server->arena, str8_lit("ramfs"));
		dir.group_id = str8_copy(request->server->arena, str8_lit("ramfs"));
		dir.modify_user_id = str8_copy(request->server->arena, str8_lit("ramfs"));
		dir.access_time = now;
		dir.modify_time = now;
	}
	else
	{
		dir.qid = request->fid->qid;
		dir.mode = 0644;
		dir.name = str8_copy(request->server->arena, entry->name);
		dir.user_id = str8_copy(request->server->arena, str8_lit("ramfs"));
		dir.group_id = str8_copy(request->server->arena, str8_lit("ramfs"));
		dir.modify_user_id = str8_copy(request->server->arena, str8_lit("ramfs"));
		dir.access_time = now;
		dir.modify_time = now;
		dir.length = entry->file ? entry->file->data.size : 0;
	}

	request->out_msg.stat_data = str8_from_dir9p(request->server->arena, dir);
	server9p_respond(request, str8_zero());
}

static void
ramfs_remove(ServerRequest9P *request)
{
	Ramentry *entry = request->fid->auxiliary;
	if(entry == 0)
	{
		server9p_respond(request, str8_lit("cannot remove root directory"));
		return;
	}

	if(filelist == entry)
	{
		filelist = entry->next;
	}
	else
	{
		for(Ramentry *prev = filelist; prev != 0; prev = prev->next)
		{
			if(prev->next == entry)
			{
				prev->next = entry->next;
				break;
			}
		}
	}

	server9p_respond(request, str8_zero());
}

static void
ramfs_open(ServerRequest9P *request)
{
	Ramentry *entry = request->fid->auxiliary;
	if(entry == 0)
	{
		if((request->in_msg.open_mode & 3) == P9_OpenFlag_Read)
		{
			request->out_msg.qid = request->fid->qid;
			server9p_respond(request, str8_zero());
			return;
		}
		else
		{
			server9p_respond(request, str8_lit("cannot write to root directory"));
			return;
		}
	}

	Ramfile *ram_file = entry->file;
	if(ram_file == 0)
	{
		ram_file = push_array(request->server->arena, Ramfile, 1);
		ram_file->arena = request->server->arena;
		ram_file->data = str8_zero();
		entry->file = ram_file;
	}
	if((request->in_msg.open_mode & P9_OpenFlag_Truncate) && ram_file->data.size > 0)
	{
		ram_file->data = str8_zero();
	}

	request->out_msg.qid = request->fid->qid;
	server9p_respond(request, str8_zero());
}

static void
entry_point(CmdLine *cmd_line)
{
	Temp scratch = scratch_begin(0, 0);
	Log *log = log_alloc();
	log_select(log);
	log_scope_begin();
	Arena *arena = arena_alloc();
	String8 address = str8_zero();

	if(cmd_line->inputs.node_count > 0)
	{
		address = cmd_line->inputs.first->string;
	}

	if(address.size == 0)
	{
		log_error(str8_lit("usage: ramfs <address>\n"
		                   "<address>: dial string (e.g., tcp!localhost!5640)\n"));
	}
	else
	{
		OS_Handle listen_socket = dial9p_listen(address, str8_lit("tcp"), str8_lit("9pfs"));
		if(os_handle_match(listen_socket, os_handle_zero()))
		{
			log_errorf("ramfs: failed to listen on address '%S'\n", address);
		}
		else
		{
			log_infof("ramfs: listening on %S\n", address);
			for(;;)
			{
				OS_Handle connection_socket = os_socket_accept(listen_socket);
				if(os_handle_match(connection_socket, os_handle_zero()))
				{
					log_error(str8_lit("ramfs: failed to accept connection\n"));
					continue;
				}
				log_info(str8_lit("ramfs: accepted connection\n"));
				u64 connection_fd = connection_socket.u64[0];
				Server9P *server = server9p_alloc(arena, connection_fd, connection_fd);
				if(server == 0)
				{
					log_error(str8_lit("ramfs: failed to allocate server\n"));
					os_file_close(connection_socket);
					continue;
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
							ramfs_version(request);
							break;
						case Msg9P_Tattach:
							ramfs_attach(request);
							break;
						case Msg9P_Twalk:
							ramfs_walk(request);
							break;
						case Msg9P_Topen:
							ramfs_open(request);
							break;
						case Msg9P_Tcreate:
							ramfs_create(request);
							break;
						case Msg9P_Tread:
							ramfs_read(request);
							break;
						case Msg9P_Twrite:
							ramfs_write(request);
							break;
						case Msg9P_Tstat:
							ramfs_stat(request);
							break;
						case Msg9P_Tremove:
							ramfs_remove(request);
							break;
						case Msg9P_Tclunk:
						{
							server9p_fid_remove(server, request->in_msg.fid);
							server9p_respond(request, str8_zero());
						}
						break;
						default:
						{
							server9p_respond(request, str8_lit("unsupported operation"));
						}
						break;
					}
				}

				os_file_close(connection_socket);
				log_info(str8_lit("ramfs: connection closed\n"));
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
