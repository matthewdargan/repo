////////////////////////////////
//~ Type Constructors

internal Message9P
msg9p_zero(void)
{
  Message9P msg = {0};
  return msg;
}

internal Dir9P
dir9p_zero(void)
{
  Dir9P dir = {0};
  return dir;
}

////////////////////////////////
//~ Encoding/Decoding Helpers

internal u8 *
encode_str8(u8 *ptr, String8 string)
{
  write_u16(ptr, from_le_u16(string.size));
  ptr += P9_STRING8_SIZE_FIELD_SIZE;

  if(string.size > 0)
  {
    MemoryCopy(ptr, string.str, string.size);
    ptr += string.size;
  }

  return ptr;
}

internal u8 *
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

internal u8 *
decode_str8(u8 *ptr, u8 *end, String8 *out_string)
{
  if(ptr + P9_STRING8_SIZE_FIELD_SIZE > end) { return 0; }

  u32 size = (u32)from_le_u16(read_u16(ptr));
  ptr += P9_STRING8_SIZE_FIELD_SIZE;

  if(ptr + size > end) { return 0; }

  out_string->size = size;
  if(size > 0)
  {
    out_string->str = ptr;
    ptr += size;
  }
  else { out_string->str = 0; }

  return ptr;
}

internal u8 *
decode_qid(u8 *ptr, u8 *end, Qid *out_qid)
{
  if(ptr + P9_QID_ENCODED_SIZE > end) { return 0; }

  out_qid->type = (u32)*ptr;
  ptr += 1;

  out_qid->version = from_le_u32(read_u32(ptr));
  ptr += 4;

  out_qid->path = from_le_u64(read_u64(ptr));
  ptr += 8;

  return ptr;
}

////////////////////////////////
//~ Message Encoding/Decoding

internal u32
msg9p_size(Message9P msg)
{
  u32 total_size = P9_MESSAGE_MINIMUM_SIZE;

  switch(msg.type)
  {
  case Msg9P_Tversion:
  case Msg9P_Rversion:
  {
    total_size += 4;
    total_size += P9_STRING8_SIZE_FIELD_SIZE + msg.protocol_version.size;
  }break;
  case Msg9P_Tauth:
  {
    total_size += 4;
    total_size += P9_STRING8_SIZE_FIELD_SIZE + msg.user_name.size;
    total_size += P9_STRING8_SIZE_FIELD_SIZE + msg.attach_path.size;
  }break;
  case Msg9P_Rauth: { total_size += P9_QID_ENCODED_SIZE; }break;
  case Msg9P_Rerror: { total_size += P9_STRING8_SIZE_FIELD_SIZE + msg.error_message.size; }break;
  case Msg9P_Tflush: { total_size += 2; }break;
  case Msg9P_Rflush: break;
  case Msg9P_Tattach:
  {
    total_size += 4;
    total_size += 4;
    total_size += P9_STRING8_SIZE_FIELD_SIZE + msg.user_name.size;
    total_size += P9_STRING8_SIZE_FIELD_SIZE + msg.attach_path.size;
  }break;
  case Msg9P_Rattach: { total_size += P9_QID_ENCODED_SIZE; }break;
  case Msg9P_Twalk:
  {
    total_size += 4;
    total_size += 4;
    total_size += 2;
    for(u64 i = 0; i < msg.walk_name_count; i += 1) { total_size += P9_STRING8_SIZE_FIELD_SIZE + msg.walk_names[i].size; }
  }break;
  case Msg9P_Rwalk:
  {
    total_size += 2;
    total_size += msg.walk_qid_count * P9_QID_ENCODED_SIZE;
  }break;
  case Msg9P_Topen:
  {
    total_size += 4;
    total_size += 1;
  }break;
  case Msg9P_Ropen:
  case Msg9P_Rcreate:
  {
    total_size += P9_QID_ENCODED_SIZE;
    total_size += 4;
  }break;
  case Msg9P_Tcreate:
  {
    total_size += 4;
    total_size += P9_STRING8_SIZE_FIELD_SIZE + msg.name.size;
    total_size += 4;
    total_size += 1;
  }break;
  case Msg9P_Tread:
  {
    total_size += 4;
    total_size += 8;
    total_size += 4;
  }break;
  case Msg9P_Rread:
  {
    total_size += 4;
    total_size += msg.payload_data.size;
  }break;
  case Msg9P_Twrite:
  {
    total_size += 4;
    total_size += 8;
    total_size += 4;
    total_size += msg.payload_data.size;
  }break;
  case Msg9P_Rwrite: { total_size += 4; }break;
  case Msg9P_Tclunk:
  case Msg9P_Tremove: { total_size += 4; }break;
  case Msg9P_Rclunk:
  case Msg9P_Rremove: break;
  case Msg9P_Tstat: { total_size += 4; }break;
  case Msg9P_Rstat: { total_size += P9_STRING8_SIZE_FIELD_SIZE + msg.stat_data.size; }break;
  case Msg9P_Twstat:
  {
    total_size += 4;
    total_size += P9_STRING8_SIZE_FIELD_SIZE + msg.stat_data.size;
  }break;
  case Msg9P_Rwstat: break;
  default: { return 0; }break;
  }
  return total_size;
}

internal String8
str8_from_msg9p(Arena *arena, Message9P msg)
{
  u32 msg_size = msg9p_size(msg);
  if(msg_size == 0) { return str8_zero(); }

  String8 result = str8_zero();
  result.str     = push_array_no_zero(arena, u8, msg_size);
  result.size    = msg_size;

  u8 *ptr = result.str;
  write_u32(ptr, from_le_u32(result.size));
  ptr += P9_MESSAGE_SIZE_FIELD_SIZE;

  *ptr = (u8)msg.type;
  ptr += P9_MESSAGE_TYPE_FIELD_SIZE;

  write_u16(ptr, from_le_u16(msg.tag));
  ptr += P9_MESSAGE_TAG_FIELD_SIZE;
  switch(msg.type)
  {
  case Msg9P_Tversion:
  case Msg9P_Rversion:
  {
    write_u32(ptr, from_le_u32(msg.max_message_size));
    ptr += 4;
    ptr = encode_str8(ptr, msg.protocol_version);
  }break;
  case Msg9P_Tauth:
  {
    write_u32(ptr, from_le_u32(msg.auth_fid));
    ptr += 4;
    ptr = encode_str8(ptr, msg.user_name);
    ptr = encode_str8(ptr, msg.attach_path);
  }break;
  case Msg9P_Rauth: { ptr = encode_qid(ptr, msg.auth_qid); }break;
  case Msg9P_Rerror: { ptr = encode_str8(ptr, msg.error_message); }break;
  case Msg9P_Tflush:
  {
    write_u16(ptr, from_le_u16(msg.cancel_tag));
    ptr += 2;
  }break;
  case Msg9P_Rflush: break;
  case Msg9P_Tattach:
  {
    write_u32(ptr, from_le_u32(msg.fid));
    ptr += 4;
    write_u32(ptr, from_le_u32(msg.auth_fid));
    ptr += 4;
    ptr = encode_str8(ptr, msg.user_name);
    ptr = encode_str8(ptr, msg.attach_path);
  }break;
  case Msg9P_Rattach: { ptr = encode_qid(ptr, msg.qid); }break;
  case Msg9P_Twalk:
  {
    write_u32(ptr, from_le_u32(msg.fid));
    ptr += 4;

    write_u32(ptr, from_le_u32(msg.new_fid));
    ptr += 4;

    write_u16(ptr, from_le_u16(msg.walk_name_count));
    ptr += 2;

    if(msg.walk_name_count > P9_MAX_WALK_ELEM_COUNT) { return str8_zero(); }

    for(u64 i = 0; i < msg.walk_name_count; i += 1)
    {
      ptr = encode_str8(ptr, msg.walk_names[i]);
    }
  }break;
  case Msg9P_Rwalk:
  {
    write_u16(ptr, from_le_u16(msg.walk_qid_count));
    ptr += 2;

    if(msg.walk_qid_count > P9_MAX_WALK_ELEM_COUNT) { return str8_zero(); }

    for(u64 i = 0; i < msg.walk_qid_count; i += 1)
    {
      ptr = encode_qid(ptr, msg.walk_qids[i]);
    }
  }break;
  case Msg9P_Topen:
  {
    write_u32(ptr, from_le_u32(msg.fid));
    ptr += 4;

    *ptr = (u8)msg.open_mode;
    ptr += 1;
  }break;
  case Msg9P_Ropen:
  case Msg9P_Rcreate:
  {
    ptr = encode_qid(ptr, msg.qid);

    write_u32(ptr, from_le_u32(msg.io_unit_size));
    ptr += 4;
  }break;
  case Msg9P_Tcreate:
  {
    write_u32(ptr, from_le_u32(msg.fid));
    ptr += 4;

    ptr = encode_str8(ptr, msg.name);

    write_u32(ptr, from_le_u32(msg.permissions));
    ptr += 4;

    *ptr = (u8)msg.open_mode;
    ptr += 1;
  }break;
  case Msg9P_Tread:
  {
    write_u32(ptr, from_le_u32(msg.fid));
    ptr += 4;

    write_u64(ptr, from_le_u64(msg.file_offset));
    ptr += 8;

    write_u32(ptr, from_le_u32(msg.byte_count));
    ptr += 4;
  }break;
  case Msg9P_Rread:
  {
    write_u32(ptr, from_le_u32(msg.payload_data.size));
    ptr += 4;
    if(msg.payload_data.size > 0)
    {
      MemoryCopy(ptr, msg.payload_data.str, msg.payload_data.size);
      ptr += msg.payload_data.size;
    }
  }break;
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
  }break;
  case Msg9P_Rwrite:
  {
    write_u32(ptr, from_le_u32(msg.byte_count));
    ptr += 4;
  }break;
  case Msg9P_Tclunk:
  case Msg9P_Tremove:
  {
    write_u32(ptr, from_le_u32(msg.fid));
    ptr += 4;
  }break;
  case Msg9P_Rclunk:
  case Msg9P_Rremove: break;
  case Msg9P_Tstat:
  {
    write_u32(ptr, from_le_u32(msg.fid));
    ptr += 4;
  }break;
  case Msg9P_Rstat:
  {
    write_u16(ptr, from_le_u16(msg.stat_data.size));
    ptr += 2;
    if(msg.stat_data.size > 0)
    {
      MemoryCopy(ptr, msg.stat_data.str, msg.stat_data.size);
      ptr += msg.stat_data.size;
    }
  }break;
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
  }break;
  case Msg9P_Rwstat: break;
  default: { return str8_zero(); }break;
  }

  if(result.size != (u64)(ptr - result.str)) { return str8_zero(); }

  return result;
}

internal Message9P
msg9p_from_str8(String8 data)
{
  Message9P result = msg9p_zero();
  if(data.size < P9_MESSAGE_MINIMUM_SIZE) { return msg9p_zero(); }

  u8 *ptr  = data.str;
  u8 *end  = data.str + data.size;
  u32 size = from_le_u32(read_u32(ptr));
  ptr     += P9_MESSAGE_SIZE_FIELD_SIZE;

  if(size != data.size) { return msg9p_zero(); }

  result.type = (u32)*ptr;
  ptr        += P9_MESSAGE_TYPE_FIELD_SIZE;

  result.tag = from_le_u16(read_u16(ptr));
  ptr       += P9_MESSAGE_TAG_FIELD_SIZE;
  switch(result.type)
  {
  case Msg9P_Tversion:
  case Msg9P_Rversion:
  {
    if(ptr + 4 > end) { return msg9p_zero(); }

    result.max_message_size = from_le_u32(read_u32(ptr));
    ptr += 4;

    ptr = decode_str8(ptr, end, &result.protocol_version);
    if(ptr == 0) { return msg9p_zero(); }
  }break;
  case Msg9P_Tauth:
  {
    if(ptr + 4 > end) { return msg9p_zero(); }

    result.auth_fid = from_le_u32(read_u32(ptr));
    ptr += 4;

    ptr = decode_str8(ptr, end, &result.user_name);
    if(ptr == 0) { return msg9p_zero(); }

    ptr = decode_str8(ptr, end, &result.attach_path);
    if(ptr == 0) { return msg9p_zero(); }
  }break;
  case Msg9P_Rauth:
  {
    ptr = decode_qid(ptr, end, &result.auth_qid);
    if(ptr == 0) { return msg9p_zero(); }
  }break;
  case Msg9P_Rerror:
  {
    ptr = decode_str8(ptr, end, &result.error_message);
    if(ptr == 0) { return msg9p_zero(); }
  }break;
  case Msg9P_Tflush:
  {
    if(ptr + 2 > end) { return msg9p_zero(); }

    result.cancel_tag = from_le_u16(read_u16(ptr));
    ptr += 2;
  }break;
  case Msg9P_Rflush: break;
  case Msg9P_Tattach:
  {
    if(ptr + 8 > end) { return msg9p_zero(); }

    result.fid = from_le_u32(read_u32(ptr));
    ptr += 4;

    result.auth_fid = from_le_u32(read_u32(ptr));
    ptr += 4;

    ptr = decode_str8(ptr, end, &result.user_name);
    if(ptr == 0) { return msg9p_zero(); }

    ptr = decode_str8(ptr, end, &result.attach_path);
    if(ptr == 0) { return msg9p_zero(); }
  }break;
  case Msg9P_Rattach:
  {
    ptr = decode_qid(ptr, end, &result.qid);
    if(ptr == 0) { return msg9p_zero(); }
  }break;
  case Msg9P_Twalk:
  {
    if(ptr + 10 > end) { return msg9p_zero(); }

    result.fid = from_le_u32(read_u32(ptr));
    ptr += 4;

    result.new_fid = from_le_u32(read_u32(ptr));
    ptr += 4;

    result.walk_name_count = from_le_u16(read_u16(ptr));
    ptr += 2;

    if(result.walk_name_count > P9_MAX_WALK_ELEM_COUNT) { return msg9p_zero(); }

    for(u64 i = 0; i < result.walk_name_count; i += 1)
    {
      ptr = decode_str8(ptr, end, &result.walk_names[i]);
      if(ptr == 0) { return msg9p_zero(); }
    }
  }break;
  case Msg9P_Rwalk:
  {
    if(ptr + 2 > end) { return msg9p_zero(); }

    result.walk_qid_count = from_le_u16(read_u16(ptr));
    ptr += 2;

    if(result.walk_qid_count > P9_MAX_WALK_ELEM_COUNT) { return msg9p_zero(); }

    for(u64 i = 0; i < result.walk_qid_count; i += 1)
    {
      ptr = decode_qid(ptr, end, &result.walk_qids[i]);
      if(ptr == 0) { return msg9p_zero(); }
    }
  }break;
  case Msg9P_Topen:
  {
    if(ptr + 5 > end) { return msg9p_zero(); }

    result.fid = from_le_u32(read_u32(ptr));
    ptr += 4;

    result.open_mode = (u32)*ptr;
    ptr += 1;
  }break;
  case Msg9P_Ropen:
  case Msg9P_Rcreate:
  {
    ptr = decode_qid(ptr, end, &result.qid);
    if(ptr == 0)      { return msg9p_zero(); }
    if(ptr + 4 > end) { return msg9p_zero(); }

    result.io_unit_size = from_le_u32(read_u32(ptr));
    ptr += 4;
  }break;
  case Msg9P_Tcreate:
  {
    if(ptr + 4 > end) { return msg9p_zero(); }

    result.fid = from_le_u32(read_u32(ptr));
    ptr += 4;

    ptr = decode_str8(ptr, end, &result.name);
    if(ptr == 0)      { return msg9p_zero(); }
    if(ptr + 5 > end) { return msg9p_zero(); }

    result.permissions = from_le_u32(read_u32(ptr));
    ptr += 4;

    result.open_mode = (u32)*ptr;
    ptr += 1;
  }break;
  case Msg9P_Tread:
  {
    if(ptr + 16 > end) { return msg9p_zero(); }

    result.fid = from_le_u32(read_u32(ptr));
    ptr += 4;

    result.file_offset = from_le_u64(read_u64(ptr));
    ptr += 8;

    result.byte_count = from_le_u32(read_u32(ptr));
    ptr += 4;
  }break;
  case Msg9P_Rread:
  {
    if(ptr + 4 > end) { return msg9p_zero(); }

    result.payload_data.size = from_le_u32(read_u32(ptr));
    ptr += 4;

    if(ptr + result.payload_data.size > end) { return msg9p_zero(); }

    if(result.payload_data.size > 0)
    {
      result.payload_data.str = ptr;
      ptr += result.payload_data.size;
    }
    else { result.payload_data.str = 0; }
  }break;
  case Msg9P_Twrite:
  {
    if(ptr + 16 > end) { return msg9p_zero(); }

    result.fid = from_le_u32(read_u32(ptr));
    ptr += 4;

    result.file_offset = from_le_u64(read_u64(ptr));
    ptr += 8;

    result.payload_data.size = from_le_u32(read_u32(ptr));
    ptr += 4;

    if(ptr + result.payload_data.size > end) { return msg9p_zero(); }

    if(result.payload_data.size > 0)
    {
      result.payload_data.str = ptr;
      ptr += result.payload_data.size;
    }
    else { result.payload_data.str = 0; }
  }break;
  case Msg9P_Rwrite:
  {
    if(ptr + 4 > end) { return msg9p_zero(); }

    result.byte_count = from_le_u32(read_u32(ptr));
    ptr += 4;
  }break;
  case Msg9P_Tclunk:
  case Msg9P_Tremove:
  {
    if(ptr + 4 > end) { return msg9p_zero(); }

    result.fid = from_le_u32(read_u32(ptr));
    ptr += 4;
  }break;
  case Msg9P_Rclunk:
  case Msg9P_Rremove: break;
  case Msg9P_Tstat:
  {
    if(ptr + 4 > end) { return msg9p_zero(); }

    result.fid = from_le_u32(read_u32(ptr));
    ptr += 4;
  }break;
  case Msg9P_Rstat:
  {
    if(ptr + P9_STRING8_SIZE_FIELD_SIZE > end) { return msg9p_zero(); }

    result.stat_data.size = from_le_u16(read_u16(ptr));
    ptr += P9_STRING8_SIZE_FIELD_SIZE;

    if(ptr + result.stat_data.size > end) { return msg9p_zero(); }

    if(result.stat_data.size > 0)
    {
      result.stat_data.str = ptr;
      ptr += result.stat_data.size;
    }
    else { result.stat_data.str = 0; }
  }break;
  case Msg9P_Twstat:
  {
    if(ptr + 6 > end) { return msg9p_zero(); }

    result.fid = from_le_u32(read_u32(ptr));
    ptr += 4;

    result.stat_data.size = from_le_u16(read_u16(ptr));
    ptr += P9_STRING8_SIZE_FIELD_SIZE;

    if(ptr + result.stat_data.size > end) { return msg9p_zero(); }

    if(result.stat_data.size > 0)
    {
      result.stat_data.str = ptr;
      ptr += result.stat_data.size;
    }
    else { result.stat_data.str = 0; }
  }break;
  case Msg9P_Rwstat: break;
  default: { return msg9p_zero(); }break;
  }

  if(ptr != end) { return msg9p_zero(); }

  return result;
}

////////////////////////////////
//~ Message Formatting

internal String8
str8_from_msg9p__fmt(Arena *arena, Message9P msg)
{
  String8 result = str8_zero();
  switch(msg.type)
  {
  case Msg9P_Tversion:
  {
    result = str8f(arena, "Msg9P_Tversion tag=%u msize=%u version='%.*s'", msg.tag, msg.max_message_size, str8_varg(msg.protocol_version));
  }break;
  case Msg9P_Rversion:
  {
    result = str8f(arena, "Msg9P_Rversion tag=%u msize=%u version='%.*s'", msg.tag, msg.max_message_size, str8_varg(msg.protocol_version));
  }break;
  case Msg9P_Tauth:
  {
    result = str8f(arena, "Msg9P_Tauth tag=%u afid=%u uname='%.*s' aname='%.*s'", msg.tag, msg.auth_fid, str8_varg(msg.user_name), str8_varg(msg.attach_path));
  }break;
  case Msg9P_Rauth:
  {
    result = str8f(arena, "Msg9P_Rauth tag=%u qid=(type=%u vers=%u path=%llu)", msg.tag, msg.auth_qid.type, msg.auth_qid.version, msg.auth_qid.path);
  }break;
  case Msg9P_Rerror: { result = str8f(arena, "Msg9P_Rerror tag=%u ename='%.*s'", msg.tag, str8_varg(msg.error_message)); }break;
  case Msg9P_Tattach:
  {
    result = str8f(arena, "Msg9P_Tattach tag=%u fid=%u afid=%u uname='%.*s' aname='%.*s'", msg.tag, msg.fid, msg.auth_fid, str8_varg(msg.user_name), str8_varg(msg.attach_path));
  }break;
  case Msg9P_Rattach:
  {
    result = str8f(arena, "Msg9P_Rattach tag=%u qid=(type=%u vers=%u path=%llu)", msg.tag, msg.qid.type, msg.qid.version, msg.qid.path);
  }break;
  case Msg9P_Twalk:
  {
    result = str8f(arena, "Msg9P_Twalk tag=%u fid=%u newfid=%u nwname=%u", msg.tag, msg.fid, msg.new_fid, msg.walk_name_count);
    for(u64 i = 0; i < msg.walk_name_count; i += 1) { result = str8f(arena, "%S '%.*s'", result, str8_varg(msg.walk_names[i])); }
  }break;
  case Msg9P_Rwalk:
  {
    result = str8f(arena, "Msg9P_Rwalk tag=%u nwqid=%u", msg.tag, msg.walk_qid_count);
    for(u64 i = 0; i < msg.walk_qid_count; i += 1)
    {
      result = str8f(arena, "%S qid%llu=(type=%u vers=%u path=%llu)", result, i, msg.walk_qids[i].type, msg.walk_qids[i].version, msg.walk_qids[i].path);
    }
  }break;
  case Msg9P_Topen: { result = str8f(arena, "Msg9P_Topen tag=%u fid=%u mode=%u", msg.tag, msg.fid, msg.open_mode); }break;
  case Msg9P_Ropen:
  {
    result = str8f(arena, "Msg9P_Ropen tag=%u qid=(type=%u vers=%u path=%llu) iounit=%u", msg.tag, msg.qid.type, msg.qid.version, msg.qid.path, msg.io_unit_size);
  }break;
  case Msg9P_Tcreate:
  {
    result = str8f(arena, "Msg9P_Tcreate tag=%u fid=%u name='%.*s' perm=%u mode=%u", msg.tag, msg.fid, str8_varg(msg.name), msg.permissions, msg.open_mode);
  }break;
  case Msg9P_Rcreate:
  {
    result = str8f(arena, "Msg9P_Rcreate tag=%u qid=(type=%u vers=%u path=%llu) iounit=%u", msg.tag, msg.qid.type, msg.qid.version, msg.qid.path, msg.io_unit_size);
  }break;
  case Msg9P_Tread:
  {
    result = str8f(arena, "Msg9P_Tread tag=%u fid=%u offset=%llu count=%u", msg.tag, msg.fid, msg.file_offset, msg.byte_count);
  }break;
  case Msg9P_Rread: { result = str8f(arena, "Msg9P_Rread tag=%u count=%llu", msg.tag, msg.payload_data.size); }break;
  case Msg9P_Twrite:
  {
    result = str8f(arena, "Msg9P_Twrite tag=%u fid=%u offset=%llu count=%llu", msg.tag, msg.fid, msg.file_offset, msg.payload_data.size);
  }break;
  case Msg9P_Rwrite:  { result = str8f(arena, "Msg9P_Rwrite tag=%u count=%u", msg.tag, msg.byte_count); }break;
  case Msg9P_Tclunk:  { result = str8f(arena, "Msg9P_Tclunk tag=%u fid=%u", msg.tag, msg.fid); }break;
  case Msg9P_Rclunk:  { result = str8f(arena, "Msg9P_Rclunk tag=%u", msg.tag); }break;
  case Msg9P_Tremove: { result = str8f(arena, "Msg9P_Tremove tag=%u fid=%u", msg.tag, msg.fid); }break;
  case Msg9P_Rremove: { result = str8f(arena, "Msg9P_Rremove tag=%u", msg.tag); }break;
  case Msg9P_Tstat:   { result = str8f(arena, "Msg9P_Tstat tag=%u fid=%u", msg.tag, msg.fid); }break;
  case Msg9P_Rstat:   { result = str8f(arena, "Msg9P_Rstat tag=%u stat.size=%llu", msg.tag, msg.stat_data.size); }break;
  case Msg9P_Twstat:  { result = str8f(arena, "Msg9P_Twstat tag=%u fid=%u stat.size=%llu", msg.tag, msg.fid, msg.stat_data.size); }break;
  case Msg9P_Rwstat:  { result = str8f(arena, "Msg9P_Rwstat tag=%u", msg.tag); }break;
  default:            { result = str8f(arena, "unknown type=%u tag=%u", msg.type, msg.tag); }break;
  }
  return result;
}

////////////////////////////////
//~ Directory Encoding/Decoding

internal u32
dir9p_size(Dir9P dir)
{
  u32 total_size = P9_STAT_DATA_FIXED_SIZE;
  total_size    += P9_STRING8_SIZE_FIELD_SIZE + dir.name.size;
  total_size    += P9_STRING8_SIZE_FIELD_SIZE + dir.user_id.size;
  total_size    += P9_STRING8_SIZE_FIELD_SIZE + dir.group_id.size;
  total_size    += P9_STRING8_SIZE_FIELD_SIZE + dir.modify_user_id.size;
  return total_size;
}

internal String8
str8_from_dir9p(Arena *arena, Dir9P dir)
{
  u32 entry_size = dir9p_size(dir);
  if(entry_size == 0) { return str8_zero(); }

  String8 encoded_dir = str8_zero();
  encoded_dir.str     = push_array_no_zero(arena, u8, entry_size);
  encoded_dir.size    = entry_size;

  u8 *ptr = encoded_dir.str;
  write_u16(ptr, from_le_u16(encoded_dir.size - P9_STRING8_SIZE_FIELD_SIZE));
  ptr += P9_STRING8_SIZE_FIELD_SIZE;

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

  if(encoded_dir.size != (u64)(ptr - encoded_dir.str)) { return str8_zero(); }

  return encoded_dir;
}

internal Dir9P
dir9p_from_str8(String8 data)
{
  Dir9P result = dir9p_zero();
  if(data.size < P9_STAT_DATA_FIXED_SIZE) { return dir9p_zero(); }

  u8 *ptr = data.str;
  u8 *end = data.str + data.size;
  ptr += P9_STRING8_SIZE_FIELD_SIZE;

  if(ptr + 39 > end) { return dir9p_zero(); }

  result.server_type = from_le_u16(read_u16(ptr));
  ptr += 2;

  result.server_dev = from_le_u32(read_u32(ptr));
  ptr += 4;

  result.qid.type = (u32)*ptr;
  ptr += 1;

  result.qid.version = from_le_u32(read_u32(ptr));
  ptr += 4;

  result.qid.path = from_le_u64(read_u64(ptr));
  ptr += 8;

  result.mode = from_le_u32(read_u32(ptr));
  ptr += 4;

  result.access_time = from_le_u32(read_u32(ptr));
  ptr += 4;

  result.modify_time = from_le_u32(read_u32(ptr));
  ptr += 4;

  result.length = from_le_u64(read_u64(ptr));
  ptr += 8;

  ptr = decode_str8(ptr, end, &result.name);
  if(ptr == 0) { return dir9p_zero(); }

  ptr = decode_str8(ptr, end, &result.user_id);
  if(ptr == 0) { return dir9p_zero(); }

  ptr = decode_str8(ptr, end, &result.group_id);
  if(ptr == 0) { return dir9p_zero(); }

  ptr = decode_str8(ptr, end, &result.modify_user_id);
  if(ptr == 0)   { return dir9p_zero(); }
  if(ptr != end) { return dir9p_zero(); }

  return result;
}

////////////////////////////////
//~ Directory List Operations

internal void
dir9p_list_push(Arena *arena, DirList9P *list, Dir9P dir)
{
  DirNode9P *node = push_array_no_zero(arena, DirNode9P, 1);
  node->dir       = dir;
  node->next      = 0;

  SLLQueuePush(list->first, list->last, node);
  list->count += 1;
}

////////////////////////////////
//~ Message I/O

internal String8
read_9p_msg(Arena *arena, u64 fd)
{
  u8  len_buf[4];
  u32 total_num_bytes_to_read      = 4;
  u32 total_num_bytes_read         = 0;
  u32 total_num_bytes_left_to_read = total_num_bytes_to_read;

  for(; total_num_bytes_left_to_read > 0;)
  {
    ssize_t read_result = read(fd, len_buf + total_num_bytes_read, total_num_bytes_left_to_read);
    if(read_result > 0)
    {
      total_num_bytes_read += read_result;
      total_num_bytes_left_to_read -= read_result;
    }
    else if(errno == EINTR) { continue; }
    else                    { return str8_zero(); }
  }

  u32     msg_size = (u32)from_le_u32(read_u32(len_buf));
  String8 msg      = str8_zero();
  msg.str  = push_array_no_zero(arena, u8, msg_size);
  msg.size = msg_size;

  MemoryCopy(msg.str, len_buf, sizeof len_buf);

  total_num_bytes_to_read      = msg.size - 4;
  total_num_bytes_read         = 0;
  total_num_bytes_left_to_read = total_num_bytes_to_read;

  for(; total_num_bytes_left_to_read > 0;)
  {
    ssize_t read_result = read(fd, msg.str + 4 + total_num_bytes_read, total_num_bytes_left_to_read);
    if(read_result > 0)
    {
      total_num_bytes_read += read_result;
      total_num_bytes_left_to_read -= read_result;
    }
    else if(errno == EINTR) { continue; }
    else                    { return str8_zero(); }
  }

  return msg;
}
