////////////////////////////////
//~ Dial String Parsing

internal u16
dial9p_resolve_port(String8 port, String8 protocol)
{
  if(str8_is_integer(port, 10)) { return (u16)u64_from_str8(port, 10); }

  Temp scratch = scratch_begin(0, 0);

  char port_buffer[1024] = {0};
  if(port.size >= sizeof port_buffer)
  {
    scratch_end(scratch);
    return 0;
  }

  MemoryCopy(port_buffer, port.str, port.size);
  port_buffer[port.size] = 0;

  char protocol_buffer[16] = {0};
  if(protocol.size > 0 && protocol.size < sizeof protocol_buffer)
  {
    MemoryCopy(protocol_buffer, protocol.str, protocol.size);
    protocol_buffer[protocol.size] = 0;
  }

  struct servent *service_entry = getservbyname(port_buffer, protocol_buffer);
  u16 result = 0;
  if(service_entry != 0)
  {
    endservent();
    result = ntohs((u16)service_entry->s_port);
  }

  scratch_end(scratch);
  return result;
}

internal Dial9PAddress
dial9p_parse(Arena *arena, String8 dial_string, String8 default_protocol, String8 default_port)
{
  Dial9PAddress result = {0};
  if(dial_string.size == 0)      { return result; }
  if(default_protocol.size == 0) { default_protocol = str8_lit("tcp"); }

  u64 bang_pos  = str8_find_needle(dial_string, 0, str8_lit("!"), 0);
  u64 colon_pos = str8_find_needle(dial_string, 0, str8_lit(":"), 0);

  if(bang_pos >= dial_string.size && os_file_path_exists(dial_string))
  {
    result.protocol = Dial9PProtocol_Unix;
    result.host     = str8_copy(arena, dial_string);
    return result;
  }

  if(bang_pos >= dial_string.size)
  {
    if(colon_pos < dial_string.size)
    {
      String8 host = str8_prefix(dial_string, colon_pos);
      String8 port = str8_skip(dial_string, colon_pos + 1);

      result.host     = str8_copy(arena, host);
      result.port     = dial9p_resolve_port(port, default_protocol);
      result.protocol = Dial9PProtocol_TCP;
    }
    else
    {
      result.host = str8_copy(arena, dial_string);
      if(default_port.size > 0)                            { result.port     = dial9p_resolve_port(default_port, default_protocol); }
      if(str8_match(default_protocol, str8_lit("tcp"), 0)) { result.protocol = Dial9PProtocol_TCP; }
    }
    return result;
  }

  String8 protocol        = str8_prefix(dial_string, bang_pos);
  String8 remainder       = str8_skip(dial_string, bang_pos + 1);
  u64     second_bang_pos = str8_find_needle(remainder, 0, str8_lit("!"), 0);

  if(str8_match(protocol, str8_lit("unix"), 0))
  {
    result.protocol = Dial9PProtocol_Unix;
    result.host     = str8_copy(arena, remainder);
    return result;
  }

  if(second_bang_pos < remainder.size)
  {
    String8 host = str8_prefix(remainder, second_bang_pos);
    String8 port = str8_skip(remainder, second_bang_pos + 1);

    result.host = str8_copy(arena, host);
    result.port = dial9p_resolve_port(port, protocol);
  }
  else
  {
    result.host = str8_copy(arena, remainder);
    if(default_port.size > 0) { result.port = dial9p_resolve_port(default_port, protocol); }
  }

  if(str8_match(protocol, str8_lit("tcp"), 0)) { result.protocol = Dial9PProtocol_TCP; }
  return result;
}

////////////////////////////////
//~ Dial Operations

internal OS_Handle
dial9p_connect(Arena *scratch, String8 dial_string, String8 default_protocol, String8 default_port)
{
  Dial9PAddress address = dial9p_parse(scratch, dial_string, default_protocol, default_port);
  if(address.host.size == 0)                      { return os_handle_zero(); }
  if(address.protocol == Dial9PProtocol_Unix)     { return os_socket_connect_unix(address.host); }
  else if(address.protocol == Dial9PProtocol_TCP) { return os_socket_connect_tcp(address.host, address.port); }
  return os_handle_zero();
}

internal OS_Handle
dial9p_listen(String8 dial_string, String8 default_protocol, String8 default_port)
{
  Temp          scratch = scratch_begin(0, 0);
  Dial9PAddress address = dial9p_parse(scratch.arena, dial_string, default_protocol, default_port);
  OS_Handle     result  = os_handle_zero();

  if(address.host.size == 0)
  {
    scratch_end(scratch);
    return result;
  }

  if(address.protocol == Dial9PProtocol_Unix)     { result = os_socket_listen_unix(address.host); }
  else if(address.protocol == Dial9PProtocol_TCP) { result = os_socket_listen_tcp(address.port); }

  scratch_end(scratch);
  return result;
}
