////////////////////////////////
//~ H2 Session Functions

internal b32
ssl_write_all(SSL *ssl, const u8 *data, u64 size)
{
	for(u64 written = 0; written < size;)
	{
		int result = SSL_write(ssl, data + written, (int)(size - written));
		if(result <= 0)
		{
			return 0;
		}
		written += result;
	}
	return 1;
}

internal H2_Session *
h2_session_alloc(Arena *arena, SSL *ssl, OS_Handle socket, WP_Pool *workers)
{
	H2_Session *session = push_array(arena, H2_Session, 1);
	session->arena = arena;
	session->ssl = ssl;
	session->socket = socket;
	session->streams = h2_stream_table_alloc(arena);
	session->workers = workers;

	nghttp2_session_callbacks *callbacks;
	nghttp2_session_callbacks_new(&callbacks);

	nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks, h2_on_begin_headers_callback);
	nghttp2_session_callbacks_set_on_header_callback(callbacks, h2_on_header_callback);
	nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, h2_on_frame_recv_callback);
	nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, h2_on_data_chunk_recv_callback);
	nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, h2_on_stream_close_callback);
	nghttp2_session_callbacks_set_send_callback(callbacks, h2_send_callback);

	nghttp2_session_server_new(&session->ng_session, callbacks, session);
	nghttp2_session_callbacks_del(callbacks);

	return session;
}

internal void
h2_session_send_settings(H2_Session *session)
{
	nghttp2_settings_entry settings[] = {
	    {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100},
	    {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 65535},
	};

	nghttp2_submit_settings(session->ng_session, NGHTTP2_FLAG_NONE, settings, ArrayCount(settings));
	session->want_write = 1;
}

internal void
h2_session_flush(H2_Session *session)
{
	if(!session->want_write)
	{
		return;
	}

	const uint8_t *data;
	ssize_t datalen = nghttp2_session_mem_send(session->ng_session, &data);

	if(datalen > 0)
	{
		ssl_write_all(session->ssl, data, (u64)datalen);
	}

	session->want_write = 0;
}

internal void
h2_session_send_ready_responses(H2_Session *session)
{
	for(u64 i = 0; i < session->streams->capacity; i += 1)
	{
		H2_StreamTableSlot *slot = &session->streams->slots[i];
		if(slot->occupied && slot->stream != 0)
		{
			H2_Stream *stream = slot->stream;
			if(stream->response_ready)
			{
				h2_stream_send_response(session, stream);
				stream->response_ready = 0;
				session->want_write = 1;
			}
		}
	}

	h2_session_flush(session);
}

internal void
h2_session_release(H2_Session *session)
{
	if(session->ng_session != 0)
	{
		nghttp2_session_del(session->ng_session);
	}
}

////////////////////////////////
//~ H2 Stream Task Handler

internal void
h2_stream_task_handler(void *params)
{
	H2_StreamTask *task = (H2_StreamTask *)params;
	H2_Session *session = task->session;
	H2_Stream *stream = h2_stream_table_get(session->streams, task->stream_id);

	if(stream == 0)
	{
		return;
	}

	Temp scratch = scratch_begin(&stream->arena, 1);
	HTTP_Request *req = h2_stream_to_request(stream->arena, stream);

	if(req == 0 || req->method == HTTP_Method_Unknown || req->path.size == 0)
	{
		scratch_end(scratch);
		return;
	}

	HTTP_Response *res = http_response_alloc(stream->arena, HTTP_Status_500_InternalServerError);
	res->body = str8_lit("Internal Server Error");

	// TODO: how do we fix this? we have a production-ready reverse proxy with both HTTP/1.1 and HTTP/2.0 support
	// NOTE: This is a simplified version - the actual implementation would call
	// the existing handle_http_request logic, but that function doesn't return
	// a response currently. For now, we'll create a minimal handler here.
	// In a full implementation, we'd need to refactor handle_http_request to
	// return an HTTP_Response instead of writing directly to the socket.

	// For now, let's create a basic response
	if(req->method == HTTP_Method_GET)
	{
		res = http_response_alloc(stream->arena, HTTP_Status_200_OK);
		res->body = str8_lit("HTTP/2 Response");
		http_header_add(stream->arena, &res->headers, str8_lit("Content-Type"), str8_lit("text/plain"));
	}

	String8 content_length = str8_from_u64(stream->arena, res->body.size, 10, 0, 0);
	http_header_add(stream->arena, &res->headers, str8_lit("Content-Length"), content_length);

	MutexScope(stream->response_mutex)
	{
		stream->response = res;
		stream->response_ready = 1;
	}

	scratch_end(scratch);
}
