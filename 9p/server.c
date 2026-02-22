////////////////////////////////
//~ Request Management

internal ServerRequest9P *
server9p_request_alloc(Server9P *server, u32 tag)
{
  u32 hash = tag % server->max_request_count;
  for(ServerRequest9P *check = server->request_table[hash]; check != 0; check = check->hash_next)
  {
    if(check->tag == tag) { return 0; }
  }

  ServerRequest9P *request = server->request_free_list;
  if(request != 0) { server->request_free_list = request->hash_next; }
  else             { request = push_array(server->arena, ServerRequest9P, 1); }

  MemoryZeroStruct(request);
  request->tag                = tag;
  request->server             = server;
  request->hash_next          = server->request_table[hash];
  server->request_table[hash] = request;
  server->request_count += 1;

  return request;
}

internal ServerRequest9P *
server9p_request_remove(Server9P *server, u32 tag)
{
  u32 hash               = tag % server->max_request_count;
  ServerRequest9P **prev = &server->request_table[hash];
  for(ServerRequest9P *request = *prev; request != 0; prev = &request->hash_next, request = request->hash_next)
  {
    if(request->tag == tag)
    {
      *prev = request->hash_next;
      server->request_count -= 1;
      return request;
    }
  }
  return 0;
}

////////////////////////////////
//~ Server Lifecycle

internal Server9P *
server9p_alloc(Arena *arena, u64 input_fd, u64 output_fd)
{
  Server9P *server          = push_array(arena, Server9P, 1);
  server->arena             = arena;
  server->input_fd          = input_fd;
  server->output_fd         = output_fd;
  server->max_message_size  = P9_IOUNIT_DEFAULT + P9_MESSAGE_HEADER_SIZE;
  server->read_buffer       = push_array(arena, u8, server->max_message_size);
  server->write_buffer      = push_array(arena, u8, server->max_message_size);
  server->max_fid_count     = 4096;
  server->fid_table         = push_array(arena, ServerFid9P *, server->max_fid_count);
  server->max_request_count = 4096;
  server->request_table     = push_array(arena, ServerRequest9P *, server->max_request_count);
  server->next_tag          = 1;
  return server;
}

////////////////////////////////
//~ Request Handling

internal ServerRequest9P *
server9p_get_request(Server9P *server)
{
  Temp    scratch = scratch_begin(&server->arena, 1);
  String8 msg     = read_9p_msg(scratch.arena, server->input_fd);

  if(msg.size == 0)
  {
    scratch_end(scratch);
    return 0;
  }

  Message9P f = msg9p_from_str8(msg);
  if(f.type == 0)
  {
    scratch_end(scratch);
    return 0;
  }

  ServerRequest9P *request = server9p_request_alloc(server, f.tag);
  if(request == 0)
  {
    request          = push_array(server->arena, ServerRequest9P, 1);
    request->tag     = f.tag;
    request->in_msg  = f;
    request->error   = str8_lit("duplicate tag");
    request->buffer  = msg.str;
    request->server  = server;
    request->scratch = scratch;
    return request;
  }

  request->server    = server;
  request->responded = 0;
  request->buffer    = msg.str;
  request->in_msg    = f;
  request->out_msg   = msg9p_zero();
  request->scratch   = scratch;
  switch(f.type)
  {
  case Msg9P_Tauth:
  {
    request->fid = server9p_fid_alloc(server, f.auth_fid);
    if(request->fid == 0) { request->error        = str8_lit("duplicate fid"); }
    else                  { request->fid->user_id = str8_copy(server->arena, f.user_name); }
  }
  break;
  case Msg9P_Tattach:
  {
    request->fid = server9p_fid_alloc(server, f.fid);
    if(request->fid == 0) { request->error        = str8_lit("duplicate fid"); }
    else                  { request->fid->user_id = str8_copy(server->arena, f.user_name); }
  }
  break;
  case Msg9P_Twalk:
  {
    request->fid = server9p_fid_lookup(server, f.fid);
    if(request->fid == 0) { request->error = str8_lit("unknown fid"); }
    else if(f.fid != f.new_fid)
    {
      request->new_fid = server9p_fid_alloc(server, f.new_fid);
      if(request->new_fid == 0) { request->error            = str8_lit("duplicate fid"); }
      else                      { request->new_fid->user_id = str8_copy(server->arena, request->fid->user_id); }
    }
    else { request->new_fid = request->fid; }
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
    if(request->fid == 0) { request->error = str8_lit("unknown fid"); }
  }
  break;
  default: break;
  }
  return request;
}

internal b32
server9p_respond(ServerRequest9P *request, String8 err)
{
  Server9P *server = request->server;
  if(request->responded != 0) { return 0; }

  request->responded    = 1;
  request->error        = err;
  request->out_msg.tag  = request->in_msg.tag;
  request->out_msg.type = request->in_msg.type + 1;

  if(err.size > 0)
  {
    request->out_msg.error_message = err;
    request->out_msg.type          = Msg9P_Rerror;
  }

  String8 buf = str8_from_msg9p(request->scratch.arena, request->out_msg);
  if(buf.size == 0)
  {
    scratch_end(request->scratch);
    request->hash_next        = server->request_free_list;
    server->request_free_list = request;
    return 0;
  }

  server9p_request_remove(server, request->in_msg.tag);

  u64 total_num_bytes_to_write      = buf.size;
  u64 total_num_bytes_written       = 0;
  u64 total_num_bytes_left_to_write = total_num_bytes_to_write;

  for(; total_num_bytes_left_to_write > 0;)
  {
    ssize_t write_result = write(server->output_fd, buf.str + total_num_bytes_written, total_num_bytes_left_to_write);
    if(write_result >= 0)
    {
      total_num_bytes_written += write_result;
      total_num_bytes_left_to_write -= write_result;
    }
    else if(errno == EINTR) { continue; }
    else
    {
      scratch_end(request->scratch);
      request->hash_next        = server->request_free_list;
      server->request_free_list = request;
      return 0;
    }
  }

  scratch_end(request->scratch);
  request->hash_next        = server->request_free_list;
  server->request_free_list = request;

  return total_num_bytes_written == total_num_bytes_to_write;
}

////////////////////////////////
//~ Fid Management

internal ServerFid9P *
server9p_fid_alloc(Server9P *server, u32 fid)
{
  u32 hash = fid % server->max_fid_count;
  for(ServerFid9P *check = server->fid_table[hash]; check != 0; check = check->hash_next)
  {
    if(check->fid == fid) { return 0; }
  }

  ServerFid9P *f = server->fid_free_list;
  if(f != 0) { server->fid_free_list = f->hash_next; }
  else       { f = push_array(server->arena, ServerFid9P, 1); }

  MemoryZeroStruct(f);
  f->fid                  = fid;
  f->open_mode            = P9_OPEN_MODE_NONE;
  f->server               = server;
  f->hash_next            = server->fid_table[hash];
  server->fid_table[hash] = f;
  server->fid_count += 1;

  return f;
}

internal ServerFid9P *
server9p_fid_lookup(Server9P *server, u32 fid)
{
  u32 hash = fid % server->max_fid_count;
  for(ServerFid9P *f = server->fid_table[hash]; f != 0; f = f->hash_next)
  {
    if(f->fid == fid) { return f; }
  }
  return 0;
}

internal ServerFid9P *
server9p_fid_remove(Server9P *server, u32 fid)
{
  u32          hash  = fid % server->max_fid_count;
  ServerFid9P **prev = &server->fid_table[hash];

  for(ServerFid9P *f = *prev; f != 0; prev = &f->hash_next, f = f->hash_next)
  {
    if(f->fid == fid)
    {
      *prev = f->hash_next;
      server->fid_count -= 1;
      return f;
    }
  }

  return 0;
}
