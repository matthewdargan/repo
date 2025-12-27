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
h2_session_alloc(Arena *arena, SSL *ssl, OS_Handle socket, WP_Pool *workers, H2_RequestHandler handler,
                 void *handler_data)
{
	H2_Session *session = push_array(arena, H2_Session, 1);
	session->arena = arena;
	session->ssl = ssl;
	session->socket = socket;
	session->streams = h2_stream_table_alloc(arena);
	session->workers = workers;
	session->request_handler = handler;
	session->request_handler_data = handler_data;

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
		log_info(str8_lit("httpproxy: h2_session_flush: no want_write\n"));
		return;
	}

	const uint8_t *data;
	ssize_t datalen = nghttp2_session_mem_send(session->ng_session, &data);

	log_infof("httpproxy: h2_session_flush: mem_send returned %lld bytes\n", (s64)datalen);

	if(datalen > 0)
	{
		b32 result = ssl_write_all(session->ssl, data, (u64)datalen);
		log_infof("httpproxy: h2_session_flush: ssl_write_all %s (%llu bytes)\n", result ? "succeeded" : "failed",
		          (u64)datalen);
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
	log_infof("httpproxy: h2_stream_task_handler ENTRY params=%p\n", params);

	if(params == 0)
	{
		log_info(str8_lit("httpproxy: h2_stream_task_handler: params is NULL\n"));
		return;
	}

	H2_StreamTask *task = (H2_StreamTask *)params;
	log_infof("httpproxy: h2_stream_task_handler task=%p\n", task);

	H2_Session *session = task->session;
	log_infof("httpproxy: h2_stream_task_handler session=%p stream_id=%d\n", session, task->stream_id);

	if(session == 0)
	{
		log_info(str8_lit("httpproxy: h2_stream_task_handler: session is NULL\n"));
		return;
	}

	log_infof("httpproxy: h2_stream_task_handler session->streams=%p\n", session->streams);
	H2_Stream *stream = h2_stream_table_get(session->streams, task->stream_id);

	log_infof("httpproxy: h2_stream_task_handler called for stream_id=%d\n", task->stream_id);

	if(stream == 0)
	{
		log_infof("httpproxy: h2_stream_task_handler: stream %d not found\n", task->stream_id);
		return;
	}

	HTTP_Request *req = h2_stream_to_request(stream->arena, stream);

	log_infof("httpproxy: h2_stream_task_handler: req=%p method=%d path.size=%llu handler=%p\n", req,
	          req ? req->method : -1, req ? req->path.size : 0, session->request_handler);

	if(req != 0 && req->method != HTTP_Method_Unknown && req->path.size > 0 && session->request_handler != 0)
	{
		log_info(str8_lit("httpproxy: h2_stream_task_handler: calling request handler\n"));
		session->request_handler(session, stream, req, session->request_handler_data);
	}
}
