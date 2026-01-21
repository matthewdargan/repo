////////////////////////////////
//~ HTTP Request

internal HTTP_Request *
http_request_parse(Arena *arena, String8 data)
{
  HTTP_Request *req = push_array(arena, HTTP_Request, 1);

  if(data.size == 0)
  {
    return req;
  }

  u64 line_end = str8_find_needle(data, 0, str8_lit("\r\n"), 0);
  if(line_end >= data.size)
  {
    return req;
  }

  String8 request_line = str8_prefix(data, line_end);
  String8 remainder = str8_skip(data, line_end + 2);

  String8List parts = str8_split(arena, request_line, (u8 *)" ", 1, 0);
  if(parts.node_count < 3)
  {
    return req;
  }

  String8Node *node = parts.first;
  String8 method_str = node->string;
  String8 uri = node->next->string;
  String8 version = node->next->next->string;

  req->method = http_method_from_str8(method_str);
  req->version = version;

  u64 query_pos = str8_find_needle(uri, 0, str8_lit("?"), 0);
  if(query_pos < uri.size)
  {
    req->path = str8_prefix(uri, query_pos);
    req->query = str8_skip(uri, query_pos + 1);
  }
  else
  {
    req->path = uri;
    req->query = str8_zero();
  }

  u64 header_end = str8_find_needle(remainder, 0, str8_lit("\r\n\r\n"), 0);
  if(header_end >= remainder.size)
  {
    header_end = remainder.size;
  }

  String8 header_data = str8_prefix(remainder, header_end);
  if(header_data.size > 0)
  {
    String8List header_lines = str8_split(arena, header_data, (u8 *)"\r\n", 2, 0);

    req->headers.count = header_lines.node_count;
    req->headers.headers = push_array(arena, HTTP_Header, req->headers.count);

    u64 i = 0;
    for(String8Node *line = header_lines.first; line != 0; line = line->next)
    {
      u64 colon = str8_find_needle(line->string, 0, str8_lit(": "), 0);
      if(colon < line->string.size)
      {
        req->headers.headers[i].name = str8_prefix(line->string, colon);
        req->headers.headers[i].value = str8_skip(line->string, colon + 2);
        i += 1;
      }
    }
    req->headers.count = i;
  }

  if(header_end + 4 < remainder.size)
  {
    String8 body_data = str8_skip(remainder, header_end + 4);
    String8 content_length = http_header_get(&req->headers, str8_lit("Content-Length"));
    if(content_length.size > 0)
    {
      u64 length = u64_from_str8(content_length, 10);
      req->body = str8_prefix(body_data, Min(length, body_data.size));
    }
    else
    {
      req->body = body_data;
    }
  }

  return req;
}
