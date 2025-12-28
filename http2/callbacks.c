////////////////////////////////
//~ nghttp2 Callbacks

internal int
h2_on_begin_headers_callback(nghttp2_session *ng_session, const nghttp2_frame *frame, void *user_data)
{
	(void)ng_session;

	H2_Session *session = (H2_Session *)user_data;

	if(session == 0 || session->arena == 0 || session->streams == 0)
	{
		return 0;
	}

	if(frame->hd.type != NGHTTP2_HEADERS || frame->headers.cat != NGHTTP2_HCAT_REQUEST)
	{
		return 0;
	}

	H2_Stream *stream = h2_stream_alloc(session->arena, frame->hd.stream_id);
	h2_stream_table_insert(session->streams, frame->hd.stream_id, stream);

	return 0;
}

internal int
h2_on_header_callback(nghttp2_session *ng_session, const nghttp2_frame *frame, const uint8_t *name, size_t namelen,
                      const uint8_t *value, size_t valuelen, uint8_t flags, void *user_data)
{
	(void)ng_session;
	(void)flags;

	H2_Session *session = (H2_Session *)user_data;

	if(session == 0 || session->streams == 0)
	{
		return 0;
	}

	H2_Stream *stream = h2_stream_table_get(session->streams, frame->hd.stream_id);

	if(stream == 0)
	{
		return 0;
	}

	String8 name_str = str8((u8 *)name, namelen);
	String8 value_str = str8((u8 *)value, valuelen);

	if(name_str.size > 0 && name_str.str[0] == ':')
	{
		if(str8_match(name_str, str8_lit(":method"), 0))
		{
			stream->method = http_method_from_str8(value_str);
		}
		else if(str8_match(name_str, str8_lit(":path"), 0))
		{
			u64 query_pos = str8_find_needle(value_str, 0, str8_lit("?"), 0);
			if(query_pos < value_str.size)
			{
				stream->path = str8_copy(stream->arena, str8_prefix(value_str, query_pos));
				stream->query = str8_copy(stream->arena, str8_skip(value_str, query_pos + 1));
			}
			else
			{
				stream->path = str8_copy(stream->arena, value_str);
				stream->query = str8_zero();
			}
		}
		else if(str8_match(name_str, str8_lit(":authority"), 0))
		{
			// Convert :authority to Host header for HTTP/1.1 compatibility
			http_header_add(stream->arena, &stream->headers, str8_lit("Host"), str8_copy(stream->arena, value_str));
		}
	}
	else
	{
		http_header_add(stream->arena, &stream->headers, str8_copy(stream->arena, name_str),
		                str8_copy(stream->arena, value_str));
	}

	return 0;
}

internal int
h2_on_frame_recv_callback(nghttp2_session *ng_session, const nghttp2_frame *frame, void *user_data)
{
	(void)ng_session;

	H2_Session *session = (H2_Session *)user_data;

	if(session == 0 || session->streams == 0)
	{
		return 0;
	}

	switch(frame->hd.type)
	{
		case NGHTTP2_HEADERS:
		{
			if(frame->headers.cat == NGHTTP2_HCAT_REQUEST)
			{
				H2_Stream *stream = h2_stream_table_get(session->streams, frame->hd.stream_id);
				if(stream != 0)
				{
					stream->headers_complete = 1;

					if(frame->hd.flags & NGHTTP2_FLAG_END_STREAM)
					{
						stream->end_stream = 1;

						// Process request inline on connection thread
						// (compression-oriented: one thread handles everything per connection)
						HTTP_Request *req = h2_stream_to_request(stream->arena, stream);
						if(req != 0 && req->method != HTTP_Method_Unknown && req->path.size > 0 && session->request_handler != 0)
						{
							session->request_handler(session, stream, req, session->request_handler_data);
						}
					}
				}
			}
		}
		break;

		case NGHTTP2_DATA:
		{
			if(frame->hd.flags & NGHTTP2_FLAG_END_STREAM)
			{
				H2_Stream *stream = h2_stream_table_get(session->streams, frame->hd.stream_id);
				if(stream != 0 && !stream->end_stream)
				{
					stream->end_stream = 1;

					// Process request inline on connection thread
					HTTP_Request *req = h2_stream_to_request(stream->arena, stream);
					if(req != 0 && req->method != HTTP_Method_Unknown && req->path.size > 0 && session->request_handler != 0)
					{
						session->request_handler(session, stream, req, session->request_handler_data);
					}
				}
			}
		}
		break;

		default:
			break;
	}

	return 0;
}

internal int
h2_on_data_chunk_recv_callback(nghttp2_session *ng_session, uint8_t flags, int32_t stream_id, const uint8_t *data,
                               size_t len, void *user_data)
{
	(void)ng_session;
	(void)flags;
	H2_Session *session = (H2_Session *)user_data;

	if(session == 0 || session->streams == 0)
	{
		return 0;
	}

	H2_Stream *stream = h2_stream_table_get(session->streams, stream_id);

	if(stream == 0)
	{
		return 0;
	}

	String8 chunk = str8_copy(stream->arena, str8((u8 *)data, len));
	str8_list_push(stream->arena, &stream->body_chunks, chunk);
	stream->total_body_size += len;

	return 0;
}

internal int
h2_on_stream_close_callback(nghttp2_session *ng_session, int32_t stream_id, uint32_t error_code, void *user_data)
{
	(void)ng_session;
	(void)stream_id;
	(void)error_code;
	(void)user_data;

	// Compression-oriented: streams live until connection closes
	// No need to free per-stream arenas - connection arena owns everything
	return 0;
}

internal ssize_t
h2_send_callback(nghttp2_session *ng_session, const uint8_t *data, size_t length, int flags, void *user_data)
{
	(void)ng_session;
	(void)flags;
	H2_Session *session = (H2_Session *)user_data;

	if(session == 0 || session->ssl == 0)
	{
		return NGHTTP2_ERR_CALLBACK_FAILURE;
	}

	if(ssl_write_all(session->ssl, data, length))
	{
		return (ssize_t)length;
	}
	return NGHTTP2_ERR_CALLBACK_FAILURE;
}
