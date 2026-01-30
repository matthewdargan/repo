////////////////////////////////
//~ Filesystem Operations

internal Auth_FS_State *
auth_fs_alloc(Arena *arena, Auth_RPC_State *rpc_state, String8 keys_path)
{
  Auth_FS_State *fs = push_array(arena, Auth_FS_State, 1);
  fs->arena = arena;
  fs->rpc_state = rpc_state;
  fs->keys_path = str8_copy(arena, keys_path);
  fs->mutex = mutex_alloc();
  return fs;
}

internal void
auth_fs_log(Auth_FS_State *fs, String8 entry)
{
  u64 timestamp = os_now_microseconds();
  String8 timestamped_entry = str8f(fs->arena, "[%llu] %S", timestamp, entry);

  MutexScope(fs->mutex)
  {
    str8_list_push(fs->arena, &fs->log_entries, timestamped_entry);
    fs->log_size += timestamped_entry.size;
  }
}

internal Auth_File_Info
auth_fs_lookup(Auth_FS_State *fs, String8 path)
{
  Auth_File_Info info = {0};

  if(path.size == 0 || str8_match(path, str8_lit("/"), 0))
  {
    info.type = Auth_File_Root;
    info.name = str8_lit("/");
    info.mode = 0555 | P9_ModeFlag_Directory;
    return info;
  }

  String8 name = path;
  if(name.str[0] == '/')
  {
    name = str8_skip(name, 1);
  }

  if(str8_match(name, str8_lit("rpc"), 0))
  {
    info.type = Auth_File_RPC;
    info.name = str8_lit("rpc");
    info.qid_path = 1;
    info.mode = 0666;
  }
  else if(str8_match(name, str8_lit("ctl"), 0))
  {
    info.type = Auth_File_Ctl;
    info.name = str8_lit("ctl");
    info.qid_path = 2;
    info.mode = 0600;
  }
  else if(str8_match(name, str8_lit("log"), 0))
  {
    info.type = Auth_File_Log;
    info.name = str8_lit("log");
    info.qid_path = 3;
    info.mode = 0400;
    MutexScope(fs->mutex)
    {
      info.size = fs->log_size;
    }
  }

  return info;
}

internal Auth_File_Info
auth_fs_stat_root(Auth_FS_State *fs)
{
  return auth_fs_lookup(fs, str8_lit("/"));
}

internal String8List
auth_fs_readdir(Arena *arena, Auth_File_Type type)
{
  String8List result = {0};

  if(type == Auth_File_Root)
  {
    str8_list_push(arena, &result, str8_lit("rpc"));
    str8_list_push(arena, &result, str8_lit("ctl"));
    str8_list_push(arena, &result, str8_lit("log"));
  }

  return result;
}

internal String8
auth_fs_read(Arena *arena, Auth_FS_State *fs, Auth_File_Type file_type, Auth_Conv *conv, u64 offset, u64 count)
{
  String8 result = str8_zero();

  switch(file_type)
  {
  case Auth_File_RPC:
  {
    Auth_RPC_Request request = {0};
    request.command = Auth_RPC_Command_Read;

    Auth_RPC_Response response = auth_rpc_execute(arena, fs->rpc_state, conv, request);

    if(response.success)
    {
      if(response.data.size > 0)
      {
        u64 available = response.data.size;
        if(offset < available)
        {
          u64 remaining = available - offset;
          u64 to_read = Min(count, remaining);
          result = str8(response.data.str + offset, to_read);
        }
      }
    }
    else if(response.error.size > 0)
    {
      result = response.error;
    }
  }
  break;

  case Auth_File_Log:
  {
    String8 log_content = str8_zero();
    MutexScope(fs->mutex)
    {
      log_content = str8_list_join(arena, fs->log_entries, 0);
    }
    if(offset < log_content.size)
    {
      u64 remaining = log_content.size - offset;
      u64 to_read = Min(count, remaining);
      result = str8(log_content.str + offset, to_read);
    }
  }
  break;

  default:
    break;
  }

  return result;
}

internal b32
auth_fs_write(Arena *arena, Auth_FS_State *fs, Auth_File_Type file_type, Auth_Conv **conv, String8 data)
{
  b32 success = 0;

  switch(file_type)
  {
  case Auth_File_RPC:
  {
    Auth_RPC_Request request = auth_rpc_parse(arena, data);

    if(request.command == Auth_RPC_Command_Start)
    {
      Auth_Conv *new_conv = 0;
      Auth_RPC_Response response = auth_rpc_handle_start(fs->rpc_state, &new_conv, request.start);

      if(response.success)
      {
        *conv = new_conv;
        success = 1;

        String8 log_entry = str8f(arena, "start: user=%S server=%S proto=%S role=%S state=%d\n", request.start.user,
                                  request.start.server, request.start.proto, request.start.role, new_conv->state);
        auth_fs_log(fs, log_entry);
      }
      else
      {
        String8 log_entry = str8f(arena, "start_failed: user=%S server=%S error=%S\n", request.start.user,
                                  request.start.server, response.error);
        auth_fs_log(fs, log_entry);
      }
    }
    else if(request.command == Auth_RPC_Command_Read)
    {
      Auth_RPC_Response response = auth_rpc_execute(arena, fs->rpc_state, *conv, request);
      success = response.success;

      if(*conv != 0 && (*conv)->state == Auth_State_ChallengeSent)
      {
        String8 log_entry = str8f(arena, "challenge_sent: user=%S\n", (*conv)->user);
        auth_fs_log(fs, log_entry);
      }
    }
    else if(request.command == Auth_RPC_Command_Write)
    {
      request.write_data = data;

      Auth_RPC_Response response = auth_rpc_execute(arena, fs->rpc_state, *conv, request);
      success = response.success;

      if(*conv != 0)
      {
        if(response.success && (*conv)->state == Auth_State_Done)
        {
          String8 log_entry = str8f(arena, "auth_complete: user=%S verified=%d\n", (*conv)->user, (*conv)->verified);
          auth_fs_log(fs, log_entry);
        }
        else if(!response.success)
        {
          String8 log_entry = str8f(arena, "auth_failed: user=%S error=%S\n", (*conv)->user, response.error);
          auth_fs_log(fs, log_entry);
        }
      }
    }
    else
    {
      Auth_RPC_Response response = auth_rpc_execute(arena, fs->rpc_state, *conv, request);
      success = response.success;
    }
  }
  break;

  case Auth_File_Ctl:
  {
    String8 trimmed = str8_skip_chop_whitespace(data);
    String8List parts = str8_split(arena, trimmed, (u8 *)" ", 1, 0);
    if(parts.node_count == 0)
    {
      break;
    }

    String8 command = parts.first->string;
    if(str8_match(command, str8_lit("register"), 0))
    {
      String8 user = str8_zero();
      String8 rp_id = str8_zero();
      String8 rp_name = str8_zero();

      for(String8Node *node = parts.first->next; node != 0; node = node->next)
      {
        String8 param = node->string;
        String8List kv = str8_split(arena, param, (u8 *)"=", 1, 0);
        if(kv.node_count != 2)
        {
          continue;
        }

        String8 key = kv.first->string;
        String8 value = kv.first->next->string;

        if(str8_match(key, str8_lit("user"), 0))
        {
          user = value;
        }
        else if(str8_match(key, str8_lit("rp_id"), 0))
        {
          rp_id = value;
        }
        else if(str8_match(key, str8_lit("rp_name"), 0))
        {
          rp_name = value;
        }
      }

      if(user.size == 0 || rp_id.size == 0 || rp_name.size == 0)
      {
        String8 log_entry = str8_lit("register_failed: missing required parameters\n");
        auth_fs_log(fs, log_entry);
        break;
      }

      Auth_Fido2_RegisterParams reg_params = {0};
      reg_params.rp_id = rp_id;
      reg_params.rp_name = rp_name;
      reg_params.user = user;

      Auth_Key new_key = {0};
      String8 error = str8_zero();
      if(!auth_fido2_register_credential(arena, reg_params, &new_key, &error))
      {
        String8 log_entry = str8f(arena, "register_failed: user=%S rp_id=%S error=%S\n", user, rp_id, error);
        auth_fs_log(fs, log_entry);
        break;
      }

      if(!auth_keyring_add(fs->rpc_state->keyring, &new_key, &error))
      {
        String8 log_entry = str8f(arena, "register_failed: user=%S rp_id=%S error=%S\n", user, rp_id, error);
        auth_fs_log(fs, log_entry);
        break;
      }

      String8 saved = auth_keyring_save(arena, fs->rpc_state->keyring);
      os_write_data_to_file_path(fs->keys_path, saved);

      String8 log_entry = str8f(arena, "register_success: user=%S rp_id=%S\n", user, rp_id);
      auth_fs_log(fs, log_entry);
      success = 1;
    }
    else if(str8_match(command, str8_lit("keygen"), 0))
    {
      String8 user = str8_zero();
      String8 server = str8_zero();
      String8 proto = str8_zero();

      for(String8Node *node = parts.first->next; node != 0; node = node->next)
      {
        String8 param = node->string;
        String8List kv = str8_split(arena, param, (u8 *)"=", 1, 0);
        if(kv.node_count != 2)
        {
          continue;
        }

        String8 key = kv.first->string;
        String8 value = kv.first->next->string;

        if(str8_match(key, str8_lit("user"), 0))
        {
          user = value;
        }
        else if(str8_match(key, str8_lit("server"), 0))
        {
          server = value;
        }
        else if(str8_match(key, str8_lit("proto"), 0))
        {
          proto = value;
        }
      }

      if(user.size == 0 || server.size == 0)
      {
        String8 log_entry = str8_lit("keygen_failed: missing user or server parameter\n");
        auth_fs_log(fs, log_entry);
        break;
      }

      if(!str8_match(proto, str8_lit("ed25519"), 0))
      {
        String8 log_entry = str8_lit("keygen_failed: only ed25519 protocol supported\n");
        auth_fs_log(fs, log_entry);
        break;
      }

      Auth_Key new_key = {0};
      new_key.type = Auth_Key_Type_Ed25519;
      new_key.user = user;
      new_key.rp_id = server;

      String8 error = str8_zero();
      if(!auth_ed25519_generate_keypair(new_key.ed25519_public_key, new_key.ed25519_private_key, &error))
      {
        String8 log_entry = str8f(arena, "keygen_failed: user=%S server=%S error=%S\n", user, server, error);
        auth_fs_log(fs, log_entry);
        break;
      }

      if(!auth_keyring_add(fs->rpc_state->keyring, &new_key, &error))
      {
        String8 log_entry = str8f(arena, "keygen_failed: user=%S server=%S error=%S\n", user, server, error);
        auth_fs_log(fs, log_entry);
        break;
      }

      String8 saved = auth_keyring_save(arena, fs->rpc_state->keyring);
      os_write_data_to_file_path(fs->keys_path, saved);

      String8 log_entry = str8f(arena, "keygen_success: user=%S server=%S\n", user, server);
      auth_fs_log(fs, log_entry);
      success = 1;
    }
  }
  break;

  default:
    break;
  }

  return success;
}
