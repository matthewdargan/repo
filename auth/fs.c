////////////////////////////////
//~ Filesystem Operations

internal Auth_FS_State *
auth_fs_alloc(Arena *arena, Auth_RPC_State *rpc_state)
{
  Auth_FS_State *fs = push_array(arena, Auth_FS_State, 1);
  fs->arena = arena;
  fs->rpc_state = rpc_state;
  return fs;
}

internal void
auth_fs_log(Auth_FS_State *fs, String8 entry)
{
  String8 entry_copy = str8_copy(fs->arena, entry);
  str8_list_push(fs->arena, &fs->log_entries, entry_copy);
  fs->log_size += entry_copy.size;
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
    info.size = fs->log_size;
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
    String8 log_content = str8_list_join(arena, fs->log_entries, 0);
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
      Auth_RPC_Response response = auth_rpc_handle_start(arena, fs->rpc_state, &new_conv, request.start);

      if(response.success)
      {
        *conv = new_conv;
        success = 1;

        String8 log_entry = str8f(arena, "start: user=%S server=%S proto=%S role=%S\n", request.start.user,
                                  request.start.server, request.start.proto, request.start.role);
        auth_fs_log(fs, log_entry);
      }
    }
    else if(request.command == Auth_RPC_Command_Write)
    {
      request.write_data = data;

      Auth_RPC_Response response = auth_rpc_execute(arena, fs->rpc_state, *conv, request);
      success = response.success;
    }
    else
    {
      Auth_RPC_Response response = auth_rpc_execute(arena, fs->rpc_state, *conv, request);
      success = response.success;
    }
  }
  break;

  default:
    break;
  }

  return success;
}
