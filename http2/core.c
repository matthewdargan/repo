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
h2_session_alloc(Arena *arena, SSL *ssl, OS_Handle socket, H2_RequestHandler handler, void *handler_data)
{
	H2_Session *session = push_array(arena, H2_Session, 1);
	session->arena = arena;
	session->ssl = ssl;
	session->socket = socket;
	session->streams = h2_stream_table_alloc(arena);
	session->request_handler = handler;
	session->request_handler_data = handler_data;
	session->session_mutex = mutex_alloc();

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

	MutexScope(session->session_mutex)
	{
		nghttp2_submit_settings(session->ng_session, NGHTTP2_FLAG_NONE, settings, ArrayCount(settings));
	}
	session->want_write = 1;
}

internal void
h2_session_flush(H2_Session *session)
{
	if(!session->want_write)
	{
		return;
	}

	// Keep flushing until nghttp2 has no more data to send
	for(;;)
	{
		b32 want_write = 0;
		MutexScope(session->session_mutex) { want_write = nghttp2_session_want_write(session->ng_session); }

		if(!want_write)
		{
			break;
		}

		const uint8_t *data = 0;
		ssize_t datalen = 0;
		MutexScope(session->session_mutex) { datalen = nghttp2_session_mem_send(session->ng_session, &data); }

		if(datalen > 0)
		{
			ssl_write_all(session->ssl, data, (u64)datalen);
		}
		else
		{
			// No more data available now, break to avoid infinite loop
			break;
		}
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
				// Hold reference to prevent stream cleanup while sending response
				h2_stream_ref_inc(stream);
				h2_stream_send_response(session, stream);
				stream->response_ready = 0;
				session->want_write = 1;
				h2_stream_ref_dec(stream, session->streams);
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
	if(params == 0)
	{
		return;
	}

	H2_StreamTask *task = (H2_StreamTask *)params;
	H2_Session *session = task->session;

	if(session == 0)
	{
		return;
	}

	H2_Stream *stream = h2_stream_table_get(session->streams, task->stream_id);

	if(stream == 0)
	{
		return;
	}

	HTTP_Request *req = h2_stream_to_request(stream->arena, stream);

	if(req != 0 && req->method != HTTP_Method_Unknown && req->path.size > 0 && session->request_handler != 0)
	{
		session->request_handler(session, stream, req, session->request_handler_data);
	}

	// Decrement reference count now that we're done processing
	h2_stream_ref_dec(stream, session->streams);
}
