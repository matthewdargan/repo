////////////////////////////////
//~ HTTP Response

internal HTTP_Response *
http_response_alloc(Arena *arena, HTTP_Status status)
{
  HTTP_Response *res = push_array(arena, HTTP_Response, 1);
  res->status_code = (u64)status;
  res->status_text = str8_from_http_status(status);
  res->version = str8_lit("HTTP/1.1");
  return res;
}

internal String8
http_response_serialize(Arena *arena, HTTP_Response *res)
{
  String8List list = {0};

  str8_list_pushf(arena, &list, "%S %u %S\r\n", res->version, res->status_code, res->status_text);

  for(u64 i = 0; i < res->headers.count; i += 1)
  {
    str8_list_pushf(arena, &list, "%S: %S\r\n", res->headers.headers[i].name, res->headers.headers[i].value);
  }

  str8_list_push(arena, &list, str8_lit("\r\n"));

  if(res->body.size > 0)
  {
    str8_list_push(arena, &list, res->body);
  }

  return str8_list_join(arena, list, 0);
}
