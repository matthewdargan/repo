#include "base/inc.h"
#include "9p/inc.h"
#include "auth/inc.h"
#include "base/inc.c"
#include "9p/inc.c"
#include "auth/inc.c"

////////////////////////////////
//~ Globals

global Auth_FS_State *auth_fs_state = 0;

////////////////////////////////
//~ Fid Auxiliary

typedef struct Auth_FidAuxiliary9P Auth_FidAuxiliary9P;
struct Auth_FidAuxiliary9P
{
  Auth_Conv *conv;
  Auth_File_Type file_type;
  Auth_FidAuxiliary9P *next;
};

internal Auth_FidAuxiliary9P *
fid_aux_alloc(Server9P *server)
{
  Auth_FidAuxiliary9P *aux = (Auth_FidAuxiliary9P *)server->fid_aux_free_list;
  if(aux != 0)
  {
    server->fid_aux_free_list = (FidAuxiliary9P *)aux->next;
  }
  else
  {
    aux = push_array_no_zero(server->arena, Auth_FidAuxiliary9P, 1);
  }
  MemoryZeroStruct(aux);
  return aux;
}

internal void
fid_aux_release(Server9P *server, Auth_FidAuxiliary9P *aux)
{
  if(aux == 0)
  {
    return;
  }
  aux->next = (Auth_FidAuxiliary9P *)server->fid_aux_free_list;
  server->fid_aux_free_list = (FidAuxiliary9P *)aux;
}

internal Auth_FidAuxiliary9P *
get_fid_aux(Server9P *server, ServerFid9P *fid)
{
  if(fid->auxiliary == 0)
  {
    fid->auxiliary = fid_aux_alloc(server);
  }
  return (Auth_FidAuxiliary9P *)fid->auxiliary;
}

////////////////////////////////
//~ 9P Operation Handlers

internal void
srv_version(ServerRequest9P *request)
{
  request->out_msg.max_message_size = request->in_msg.max_message_size;
  request->out_msg.protocol_version = request->in_msg.protocol_version;
  request->server->max_message_size = request->in_msg.max_message_size;
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
  Auth_File_Info root = auth_fs_stat_root(auth_fs_state);
  request->fid->qid.type = QidTypeFlag_Directory;
  request->fid->qid.path = root.qid_path;
  request->fid->qid.version = root.qid_version;
  request->out_msg.qid = request->fid->qid;

  Auth_FidAuxiliary9P *aux = get_fid_aux(request->server, request->fid);
  aux->file_type = Auth_File_Root;

  server9p_respond(request, str8_zero());
}

internal void
srv_walk(ServerRequest9P *request)
{
  Auth_FidAuxiliary9P *from_aux = get_fid_aux(request->server, request->fid);

  if(request->in_msg.walk_name_count == 0)
  {
    Auth_FidAuxiliary9P *new_aux = get_fid_aux(request->server, request->new_fid);
    new_aux->file_type = from_aux->file_type;
    new_aux->conv = from_aux->conv;
    request->new_fid->qid = request->fid->qid;
    request->out_msg.walk_qid_count = 0;
    server9p_respond(request, str8_zero());
    return;
  }

  Auth_File_Type current_type = from_aux->file_type;

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

    if(current_type != Auth_File_Root)
    {
      request->out_msg.walk_qid_count = i;
      server9p_respond(request, i == 0 ? str8_lit("not a directory") : str8_zero());
      return;
    }

    Auth_File_Info info = auth_fs_lookup(auth_fs_state, name);
    if(info.type == Auth_File_None)
    {
      request->out_msg.walk_qid_count = i;
      server9p_respond(request, i == 0 ? str8_lit("file not found") : str8_zero());
      return;
    }

    request->out_msg.walk_qids[i].type = (info.mode & P9_ModeFlag_Directory) ? QidTypeFlag_Directory : QidTypeFlag_File;
    request->out_msg.walk_qids[i].path = info.qid_path;
    request->out_msg.walk_qids[i].version = info.qid_version;
    current_type = info.type;
  }

  Auth_FidAuxiliary9P *new_aux = get_fid_aux(request->server, request->new_fid);
  new_aux->file_type = current_type;
  new_aux->conv = from_aux->conv;
  request->new_fid->qid = request->out_msg.walk_qids[request->in_msg.walk_name_count - 1];
  request->out_msg.walk_qid_count = request->in_msg.walk_name_count;
  server9p_respond(request, str8_zero());
}

internal void
srv_open(ServerRequest9P *request)
{
  request->out_msg.qid = request->fid->qid;
  request->out_msg.io_unit_size = request->server->max_message_size - 24;
  server9p_respond(request, str8_zero());
}

internal void
srv_read(ServerRequest9P *request)
{
  Auth_FidAuxiliary9P *aux = get_fid_aux(request->server, request->fid);

  if(aux->file_type == Auth_File_Root)
  {
    String8List entries = auth_fs_readdir(request->scratch.arena, Auth_File_Root);
    u64 offset = request->in_msg.file_offset;
    u64 count = request->in_msg.byte_count;

    String8List dir_bytes_list = {0};
    for(String8Node *node = entries.first; node != 0; node = node->next)
    {
      Auth_File_Info info = auth_fs_lookup(auth_fs_state, node->string);
      Dir9P dir = dir9p_zero();
      dir.qid.type = (info.mode & P9_ModeFlag_Directory) ? QidTypeFlag_Directory : QidTypeFlag_File;
      dir.qid.path = info.qid_path;
      dir.qid.version = info.qid_version;
      dir.mode = info.mode;
      dir.length = info.size;
      dir.name = info.name;
      dir.user_id = str8_lit("auth");
      dir.group_id = str8_lit("auth");
      dir.modify_user_id = str8_lit("auth");

      String8 dir_bytes = str8_from_dir9p(request->scratch.arena, dir);
      str8_list_push(request->scratch.arena, &dir_bytes_list, dir_bytes);
    }

    String8 all_dir_data = str8_list_join(request->scratch.arena, dir_bytes_list, 0);

    String8 result = str8_zero();
    if(offset < all_dir_data.size)
    {
      u64 remaining = all_dir_data.size - offset;
      u64 to_read = Min(count, remaining);
      result = str8(all_dir_data.str + offset, to_read);
    }

    request->out_msg.payload_data = result;
    request->out_msg.byte_count = result.size;
    server9p_respond(request, str8_zero());
  }
  else
  {
    String8 data = auth_fs_read(request->scratch.arena, auth_fs_state, aux->file_type, aux->conv,
                                request->in_msg.file_offset, request->in_msg.byte_count);
    request->out_msg.payload_data = data;
    request->out_msg.byte_count = data.size;
    server9p_respond(request, str8_zero());
  }
}

internal void
srv_write(ServerRequest9P *request)
{
  Auth_FidAuxiliary9P *aux = get_fid_aux(request->server, request->fid);

  b32 success =
      auth_fs_write(request->scratch.arena, auth_fs_state, aux->file_type, &aux->conv, request->in_msg.payload_data);

  if(success)
  {
    request->out_msg.byte_count = request->in_msg.payload_data.size;
    server9p_respond(request, str8_zero());
  }
  else
  {
    server9p_respond(request, str8_lit("write failed"));
  }
}

internal void
srv_stat(ServerRequest9P *request)
{
  Auth_FidAuxiliary9P *aux = get_fid_aux(request->server, request->fid);
  Auth_File_Info info = {0};

  if(aux->file_type == Auth_File_Root)
  {
    info = auth_fs_stat_root(auth_fs_state);
  }
  else if(aux->file_type == Auth_File_RPC)
  {
    info = auth_fs_lookup(auth_fs_state, str8_lit("rpc"));
  }
  else if(aux->file_type == Auth_File_Ctl)
  {
    info = auth_fs_lookup(auth_fs_state, str8_lit("ctl"));
  }
  else if(aux->file_type == Auth_File_Log)
  {
    info = auth_fs_lookup(auth_fs_state, str8_lit("log"));
  }

  Dir9P dir = dir9p_zero();
  dir.qid.type = (info.mode & P9_ModeFlag_Directory) ? QidTypeFlag_Directory : QidTypeFlag_File;
  dir.qid.path = info.qid_path;
  dir.qid.version = info.qid_version;
  dir.mode = info.mode;
  dir.length = info.size;
  dir.name = info.name;
  dir.user_id = str8_lit("auth");
  dir.group_id = str8_lit("auth");
  dir.modify_user_id = str8_lit("auth");

  String8 dir_bytes = str8_from_dir9p(request->scratch.arena, dir);
  request->out_msg.stat_data = dir_bytes;
  server9p_respond(request, str8_zero());
}

internal void
srv_clunk(ServerRequest9P *request)
{
  Auth_FidAuxiliary9P *aux = get_fid_aux(request->server, request->fid);
  fid_aux_release(request->server, aux);
  ServerFid9P *fid = server9p_fid_remove(request->server, request->in_msg.fid);
  if(fid != 0)
  {
    fid->hash_next = request->server->fid_free_list;
    request->server->fid_free_list = fid;
  }
  server9p_respond(request, str8_zero());
}

internal void
srv_wstat(ServerRequest9P *request)
{
  server9p_respond(request, str8_zero());
}

internal void
srv_remove(ServerRequest9P *request)
{
  server9p_respond(request, str8_lit("cannot remove files"));
}

internal void
srv_create(ServerRequest9P *request)
{
  server9p_respond(request, str8_lit("cannot create files"));
}

internal void
srv_flush(ServerRequest9P *request)
{
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
  log_info(str8_lit("9auth: connection established\n"));

  Server9P *server = server9p_alloc(connection_arena, connection_fd, connection_fd);
  if(server == 0)
  {
    log_error(str8_lit("9auth: failed to allocate server\n"));
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
    case Msg9P_Tclunk:
      srv_clunk(request);
      break;
    case Msg9P_Tremove:
      srv_remove(request);
      break;
    case Msg9P_Tcreate:
      srv_create(request);
      break;
    case Msg9P_Tflush:
      srv_flush(request);
      break;
    default:
      server9p_respond(request, str8_lit("unsupported operation"));
      break;
    }
  }

  os_file_close(connection_socket);
  log_info(str8_lit("9auth: connection closed\n"));
  log_scope_flush(scratch.arena);
  log_release(log);
  arena_release(connection_arena);
  scratch_end(scratch);
}

////////////////////////////////
//~ Entry Point

internal void
entry_point(CmdLine *cmd_line)
{
  Arena *arena = arena_alloc();
  Log *log = log_alloc();
  log_select(log);
  log_scope_begin();

  b32 add_credential = cmd_line_has_flag(cmd_line, str8_lit("a"));
  b32 list_credentials = cmd_line_has_flag(cmd_line, str8_lit("l"));
  b32 export_credential = cmd_line_has_flag(cmd_line, str8_lit("export"));
  b32 import_credential = cmd_line_has_flag(cmd_line, str8_lit("import"));

  if(export_credential)
  {
    String8 user = cmd_line_string(cmd_line, str8_lit("user"));
    String8 rp_id = cmd_line_string(cmd_line, str8_lit("rp_id"));

    if(user.size == 0 || rp_id.size == 0)
    {
      log_error(str8_lit("usage: 9auth --export --user=<user> --rp_id=<rp_id>\n"));
      log_scope_flush(arena);
      log_release(log);
      return;
    }

    String8 keys_path = str8_lit("/var/lib/9auth/keys");
    String8 data = os_data_from_file_path(arena, keys_path);
    if(data.size == 0)
    {
      log_error(str8_lit("9auth: no credentials found\n"));
      log_scope_flush(arena);
      log_release(log);
      return;
    }

    Auth_KeyRing ring = auth_keyring_alloc(arena, 0);
    if(!auth_keyring_load(arena, &ring, data))
    {
      log_error(str8_lit("9auth: failed to load credentials\n"));
      log_scope_flush(arena);
      log_release(log);
      return;
    }

    Auth_Key *key = auth_keyring_lookup(&ring, user, rp_id);
    if(key == 0)
    {
      log_errorf("9auth: no credential found for user='%S' rp_id='%S'\n", user, rp_id);
      log_scope_flush(arena);
      log_release(log);
      return;
    }

    Auth_KeyRing export_ring = auth_keyring_alloc(arena, 1);
    String8 add_error = str8_zero();
    if(!auth_keyring_add(&export_ring, key, &add_error))
    {
      log_errorf("9auth: failed to add credential: %S\n", add_error);
      log_scope_flush(arena);
      log_release(log);
      return;
    }

    String8 exported = auth_keyring_save(arena, &export_ring);
    fwrite(exported.str, 1, exported.size, stdout);
    fflush(stdout);

    log_scope_flush(arena);
    log_release(log);
    return;
  }
  else if(import_credential)
  {
    String8List input_chunks = {0};
    u8 buffer[4096];
    for(;;)
    {
      ssize_t n = read(STDIN_FILENO, buffer, sizeof(buffer));
      if(n <= 0)
      {
        break;
      }
      String8 chunk = str8_copy(arena, str8(buffer, n));
      str8_list_push(arena, &input_chunks, chunk);
    }

    String8 input = str8_list_join(arena, input_chunks, 0);
    if(input.size == 0)
    {
      log_error(str8_lit("9auth: no data provided on stdin\n"));
      log_scope_flush(arena);
      log_release(log);
      return;
    }

    Auth_KeyRing import_ring = auth_keyring_alloc(arena, 0);
    if(!auth_keyring_load(arena, &import_ring, input))
    {
      log_error(str8_lit("9auth: failed to parse imported data\n"));
      log_scope_flush(arena);
      log_release(log);
      return;
    }

    if(import_ring.count == 0)
    {
      log_error(str8_lit("9auth: no credentials in imported data\n"));
      log_scope_flush(arena);
      log_release(log);
      return;
    }

    String8 keys_path = str8_lit("/var/lib/9auth/keys");
    String8 existing = os_data_from_file_path(arena, keys_path);
    Auth_KeyRing ring = auth_keyring_alloc(arena, 0);

    if(existing.size > 0)
    {
      auth_keyring_load(arena, &ring, existing);
    }

    for(u64 i = 0; i < import_ring.count; i += 1)
    {
      Auth_Key *key = &import_ring.keys[i];
      Auth_Key *existing_key = auth_keyring_lookup(&ring, key->user, key->rp_id);
      if(existing_key != 0)
      {
        auth_keyring_remove(&ring, key->user, key->rp_id);
      }
      String8 add_error = str8_zero();
      if(!auth_keyring_add(&ring, key, &add_error))
      {
        log_errorf("9auth: failed to import credential for user='%S': %S\n", key->user, add_error);
        continue;
      }
    }

    String8 saved = auth_keyring_save(arena, &ring);
    os_write_data_to_file_path(keys_path, saved);

    log_infof("9auth: imported %llu credentials\n", import_ring.count);
    log_scope_flush(arena);
    log_release(log);
    return;
  }
  else if(add_credential)
  {
    String8 user = cmd_line_string(cmd_line, str8_lit("user"));
    String8 rp_id = cmd_line_string(cmd_line, str8_lit("rp_id"));
    String8 rp_name = cmd_line_string(cmd_line, str8_lit("rp_name"));

    if(user.size == 0 || rp_id.size == 0)
    {
      log_error(str8_lit("usage: 9auth -a --user=<user> --rp_id=<rp_id> [--rp_name=<name>]\n"));
      log_scope_flush(arena);
      log_release(log);
      return;
    }

    if(rp_name.size == 0)
    {
      rp_name = rp_id;
    }

    Auth_Fido2_RegisterParams params = {0};
    params.user = user;
    params.rp_id = rp_id;
    params.rp_name = rp_name;

    Auth_Key new_key = {0};
    String8 error = str8_zero();

    log_info(str8_lit("9auth: touch yubikey\n"));
    log_scope_flush(arena);

    if(auth_fido2_register_credential(arena, params, &new_key, &error))
    {
      String8 keys_path = str8_lit("/var/lib/9auth/keys");
      String8 existing = os_data_from_file_path(arena, keys_path);
      Auth_KeyRing ring = auth_keyring_alloc(arena, 0);

      if(existing.size > 0)
      {
        auth_keyring_load(arena, &ring, existing);
      }

      String8 add_error = str8_zero();
      if(!auth_keyring_add(&ring, &new_key, &add_error))
      {
        log_errorf("9auth: failed to add credential: %S\n", add_error);
      }
      else
      {
        String8 saved = auth_keyring_save(arena, &ring);
        os_write_data_to_file_path(keys_path, saved);
        log_info(str8_lit("9auth: registered credential\n"));
      }
    }
    else
    {
      log_errorf("9auth: registration failed: %S\n", error);
    }

    log_scope_flush(arena);
    log_release(log);
    return;
  }
  else if(list_credentials)
  {
    String8 keys_path = str8_lit("/var/lib/9auth/keys");
    String8 data = os_data_from_file_path(arena, keys_path);
    Auth_KeyRing ring = auth_keyring_alloc(arena, 0);

    if(data.size == 0)
    {
      log_info(str8_lit("9auth: no credentials\n"));
    }
    else if(auth_keyring_load(arena, &ring, data))
    {
      for(u64 i = 0; i < ring.count; i += 1)
      {
        Auth_Key *key = &ring.keys[i];
        log_infof("9auth: user=%S rp_id=%S\n", key->user, key->rp_id);
      }
    }
    else
    {
      log_error(str8_lit("9auth: failed to load credentials\n"));
    }

    log_scope_flush(arena);
    log_release(log);
    return;
  }

  String8 keys_path = str8_lit("/var/lib/9auth/keys");
  {
    struct stat st;
    if(stat((char *)keys_path.str, &st) == 0)
    {
      mode_t mode = st.st_mode & 0777;
      if(mode != 0600)
      {
        log_errorf("9auth: security error: key file has insecure permissions %o (expected 0600)\n", mode);
        log_scope_flush(arena);
        log_release(log);
        return;
      }
    }
  }

  String8 data = os_data_from_file_path(arena, keys_path);
  Auth_KeyRing ring = auth_keyring_alloc(arena, 0);

  if(data.size > 0)
  {
    if(!auth_keyring_load(arena, &ring, data))
    {
      log_error(str8_lit("9auth: failed to load keyring\n"));
      log_scope_flush(arena);
      log_release(log);
      return;
    }
  }

  Auth_RPC_State *rpc_state = auth_rpc_state_alloc(arena, &ring);
  auth_fs_state = auth_fs_alloc(arena, rpc_state);

  String8 socket_path = str8_lit("unix!/var/run/9auth");
  OS_Handle listen_socket = dial9p_listen(socket_path, str8_lit("unix"), str8_lit("9auth"));

  if(os_handle_match(listen_socket, os_handle_zero()))
  {
    log_error(str8_lit("9auth: failed to create socket at /var/run/9auth\n"));
    log_scope_flush(arena);
    log_release(log);
    return;
  }

  log_info(str8_lit("9auth: daemon started on /var/run/9auth\n"));
  log_scope_flush(arena);

  for(;;)
  {
    OS_Handle connection_socket = os_socket_accept(listen_socket);
    if(os_handle_match(connection_socket, os_handle_zero()))
    {
      continue;
    }
    handle_connection(connection_socket);
  }

  arena_release(arena);
}
