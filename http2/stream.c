////////////////////////////////
//~ H2 Stream Functions

internal H2_Stream *
h2_stream_alloc(Arena *arena, s32 stream_id)
{
	H2_Stream *stream = push_array(arena, H2_Stream, 1);
	stream->stream_id = stream_id;
	stream->arena = arena_alloc();
	stream->method = HTTP_Method_Unknown;
	return stream;
}

internal HTTP_Request *
h2_stream_to_request(Arena *arena, H2_Stream *stream)
{
	HTTP_Request *req = push_array(arena, HTTP_Request, 1);
	req->method = stream->method;
	req->path = stream->path;
	req->query = stream->query;
	req->version = str8_lit("HTTP/2.0");
	req->headers = stream->headers;

	if(stream->body_chunks.node_count > 0)
	{
		req->body = str8_list_join(arena, &stream->body_chunks, 0);
	}

	return req;
}

internal ssize_t
h2_data_source_read_callback(nghttp2_session *ng_session, int32_t stream_id, uint8_t *buf, size_t length,
                             uint32_t *data_flags, nghttp2_data_source *source, void *user_data)
{
	(void)ng_session;
	(void)stream_id;
	(void)user_data;

	H2_Stream *stream = (H2_Stream *)source->ptr;
	HTTP_Response *res = stream->response;

	fprintf(stderr, "httpproxy: data_callback (stream_id=%d, res=%p)\n", stream_id, (void *)res);
	fflush(stderr);

	if(res == 0)
	{
		fprintf(stderr, "httpproxy: data_callback -> EOF (no response)\n");
		fflush(stderr);
		*data_flags |= NGHTTP2_DATA_FLAG_EOF;
		return 0;
	}

	fprintf(stderr, "httpproxy: data_callback (offset=%llu, body.size=%llu, backend=%p, backend_state=%d)\n",
	        (unsigned long long)stream->response_body_offset, (unsigned long long)res->body.size, (void *)stream->backend,
	        stream->backend ? stream->backend->state : -1);
	fflush(stderr);

	// Check if we've caught up to current buffer
	if(stream->response_body_offset >= res->body.size)
	{
		// Check if backend still streaming (more data expected)
		Backend_Connection *backend = stream->backend;
		if(backend != 0 && backend->state == BACKEND_HEADERS_PARSED)
		{
			fprintf(stderr, "httpproxy: data_callback -> DEFERRED (backend still streaming)\n");
			fflush(stderr);
			// More data expected, defer until next read
			return NGHTTP2_ERR_DEFERRED;
		}

		fprintf(stderr, "httpproxy: data_callback -> EOF (all data sent)\n");
		fflush(stderr);
		// All data sent
		*data_flags |= NGHTTP2_DATA_FLAG_EOF;
		return 0;
	}

	u64 remaining = res->body.size - stream->response_body_offset;
	u64 to_copy = remaining < length ? remaining : length;

	MemoryCopy(buf, res->body.str + stream->response_body_offset, to_copy);
	stream->response_body_offset += to_copy;

	fprintf(stderr, "httpproxy: data_callback -> copied %llu bytes (new_offset=%llu)\n", (unsigned long long)to_copy,
	        (unsigned long long)stream->response_body_offset);
	fflush(stderr);

	// Check if we've sent all currently available data
	if(stream->response_body_offset >= res->body.size)
	{
		Backend_Connection *backend = stream->backend;
		if(backend != 0 && backend->state == BACKEND_HEADERS_PARSED)
		{
			// More data may arrive, don't set EOF yet
			fprintf(stderr, "httpproxy: data_callback -> no EOF yet (backend still active)\n");
			fflush(stderr);
		}
		else
		{
			// Backend done, this is final data
			fprintf(stderr, "httpproxy: data_callback -> setting EOF (backend done)\n");
			fflush(stderr);
			*data_flags |= NGHTTP2_DATA_FLAG_EOF;
		}
	}

	return (ssize_t)to_copy;
}

internal void
h2_stream_send_response(H2_Session *session, H2_Stream *stream)
{
	if(stream->response == 0)
	{
		return;
	}

	HTTP_Response *res = stream->response;

	// Allocate from stream arena so data lives until stream cleanup
	String8 status_str = str8_from_u64(stream->arena, (u64)res->status_code, 10, 0, 0);

	u64 nv_count = 1 + res->headers.count;
	nghttp2_nv *nva = push_array(stream->arena, nghttp2_nv, nv_count);

	nva[0] = (nghttp2_nv){
	    (uint8_t *)":status", (uint8_t *)status_str.str, 7, status_str.size, NGHTTP2_NV_FLAG_NONE,
	};

	for(u64 i = 0; i < res->headers.count; i += 1)
	{
		nva[i + 1] = (nghttp2_nv){
		    (uint8_t *)res->headers.headers[i].name.str,
		    (uint8_t *)res->headers.headers[i].value.str,
		    res->headers.headers[i].name.size,
		    res->headers.headers[i].value.size,
		    NGHTTP2_NV_FLAG_NONE,
		};
	}

	// Submit response to nghttp2
	// For backend-proxied responses, always use data provider (body may arrive later)
	// For non-backend responses, only use data provider if body is non-empty
	if(res->body.size > 0 || stream->backend != 0)
	{
		nghttp2_data_provider data_prd;
		data_prd.source.ptr = stream;
		data_prd.read_callback = h2_data_source_read_callback;
		nghttp2_submit_response(session->ng_session, stream->stream_id, nva, nv_count, &data_prd);
	}
	else
	{
		nghttp2_submit_response(session->ng_session, stream->stream_id, nva, nv_count, 0);
	}

	session->want_write = 1;
}

////////////////////////////////
//~ H2 Stream Table Functions

internal H2_StreamTableSlot *
h2_stream_table_find_slot(H2_StreamTable *table, s32 stream_id)
{
	u64 index = (u64)stream_id % table->capacity;

	for(u64 i = 0; i < table->capacity; i += 1)
	{
		u64 slot_index = (index + i) % table->capacity;
		H2_StreamTableSlot *slot = &table->slots[slot_index];

		if(slot->occupied && slot->stream_id == stream_id)
		{
			return slot;
		}

		if(!slot->occupied)
		{
			return 0;
		}
	}

	return 0;
}

internal H2_StreamTable *
h2_stream_table_alloc(Arena *arena)
{
	H2_StreamTable *table = push_array(arena, H2_StreamTable, 1);
	table->arena = arena;
	table->capacity = h2_stream_table_initial_capacity;
	table->slots = push_array(arena, H2_StreamTableSlot, table->capacity);
	return table;
}

internal void
h2_stream_table_insert(H2_StreamTable *table, s32 stream_id, H2_Stream *stream)
{
	u64 index = (u64)stream_id % table->capacity;

	for(u64 i = 0; i < table->capacity; i += 1)
	{
		u64 slot_index = (index + i) % table->capacity;
		H2_StreamTableSlot *slot = &table->slots[slot_index];

		if(!slot->occupied)
		{
			slot->occupied = 1;
			slot->stream_id = stream_id;
			slot->stream = stream;
			table->count += 1;
			return;
		}
	}

	Assert(!"Stream table full");
}

internal H2_Stream *
h2_stream_table_get(H2_StreamTable *table, s32 stream_id)
{
	H2_StreamTableSlot *slot = h2_stream_table_find_slot(table, stream_id);
	return slot != 0 ? slot->stream : 0;
}

internal void
h2_stream_table_remove(H2_StreamTable *table, s32 stream_id)
{
	H2_StreamTableSlot *slot = h2_stream_table_find_slot(table, stream_id);
	if(slot != 0)
	{
		slot->occupied = 0;
		slot->stream_id = 0;
		slot->stream = 0;
		table->count -= 1;
	}
}

////////////////////////////////
//~ Backend Connection Functions

internal Backend_Connection *
backend_connection_alloc(Arena *arena, s32 stream_id)
{
	Backend_Connection *backend = push_array(arena, Backend_Connection, 1);
	backend->stream_id = stream_id;
	backend->state = BACKEND_IDLE;
	backend->socket = os_handle_zero();
	return backend;
}

internal void
backend_connection_start(Backend_Connection *backend, String8 host, u16 port, String8 request_data)
{
	backend->request_data = request_data;
	backend->bytes_sent = 0;
	backend->response_buffer = 0;
	backend->response_size = 0;
	backend->response_capacity = 0;
	backend->response_header_end = 0;
	backend->chunked_state = CHUNK_NONE;
	backend->chunk_remaining = 0;
	backend->decoded_body_size = 0;
	backend->decoded_body_buffer = 0;
	backend->decoded_body_capacity = 0;
	backend->time_start = os_now_microseconds();
	backend->time_connected = 0;
	backend->time_request_sent = 0;
	backend->time_first_byte = 0;

	fprintf(stderr, "httpproxy: connecting to backend %.*s:%u\n", (int)host.size, host.str, port);
	fflush(stderr);

	// Use regular socket connect (blocking version, we'll set non-blocking after)
	backend->socket = os_socket_connect_tcp(host, port);
	if(os_handle_match(backend->socket, os_handle_zero()))
	{
		fprintf(stderr, "httpproxy: backend connection failed\n");
		fflush(stderr);
		backend->state = BACKEND_ERROR;
		return;
	}

	backend->time_connected = os_now_microseconds();

	fprintf(stderr, "httpproxy: backend connected successfully\n");
	fflush(stderr);

	// Set non-blocking mode for async I/O
	int fd = (int)backend->socket.u64[0];
	int flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	// Since connect already completed (blocking), we can start sending
	backend->state = BACKEND_SENDING_REQUEST;
}

internal void
backend_handle_write(Backend_Connection *backend)
{
	int fd = (int)backend->socket.u64[0];

	if(backend->state == BACKEND_CONNECTING)
	{
		// Check if connect() finished
		int error = 0;
		socklen_t len = sizeof(error);
		getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len);

		if(error == 0)
		{
			backend->state = BACKEND_SENDING_REQUEST;
		}
		else
		{
			backend->state = BACKEND_ERROR;
			return;
		}
	}

	if(backend->state == BACKEND_SENDING_REQUEST)
	{
		// Send more of the request
		u64 remaining = backend->request_data.size - backend->bytes_sent;
		ssize_t sent = write(fd, backend->request_data.str + backend->bytes_sent, remaining);

		if(sent > 0)
		{
			backend->bytes_sent += sent;

			if(backend->bytes_sent >= backend->request_data.size)
			{
				// Request fully sent, start reading response
				backend->time_request_sent = os_now_microseconds();
				fprintf(stderr, "httpproxy: backend request sent (%llu bytes), now reading response\n",
				        (unsigned long long)backend->bytes_sent);
				fflush(stderr);
				backend->state = BACKEND_READING_RESPONSE;
			}
		}
		else if(sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
		{
			fprintf(stderr, "httpproxy: backend write error: %s\n", strerror(errno));
			fflush(stderr);
			backend->state = BACKEND_ERROR;
		}
	}
}

internal b32
backend_try_parse_headers(Backend_Connection *backend, H2_Session *session)
{
	// Only parse once
	if(backend->response_header_end != 0)
	{
		return 0; // Already parsed
	}

	// Search for header/body separator
	String8 response_data = {backend->response_buffer, backend->response_size};
	u64 header_end = str8_find_needle(response_data, 0, str8_lit("\r\n\r\n"), 0);

	if(header_end >= response_data.size)
	{
		return 0; // Not found yet, need more data
	}

	// Mark header boundary
	backend->response_header_end = header_end + 4;

	// Get stream
	H2_Stream *stream = h2_stream_table_get(session->streams, backend->stream_id);
	if(stream == 0)
	{
		fprintf(stderr, "httpproxy: ERROR: stream not found in backend_try_parse_headers (stream_id=%d)\n",
		        backend->stream_id);
		fflush(stderr);
		backend->state = BACKEND_ERROR;
		return 0;
	}

	// Extract headers section
	String8 headers_section = str8_prefix(response_data, header_end);

	// Parse status line
	u64 status_line_end = str8_find_needle(headers_section, 0, str8_lit("\r\n"), 0);
	String8 status_line = str8_prefix(headers_section, status_line_end);

	// Extract status code (e.g., "HTTP/1.1 200 OK")
	u64 first_space = str8_find_needle(status_line, 0, str8_lit(" "), 0);
	String8 after_version = str8_skip(status_line, first_space + 1);
	u64 second_space = str8_find_needle(after_version, 0, str8_lit(" "), 0);
	String8 status_code_str = str8_prefix(after_version, second_space);
	u64 status_code = u64_from_str8(status_code_str, 10);

	// Validate status code
	if(status_code == 0 || status_code < 100 || status_code > 599)
	{
		fprintf(stderr, "httpproxy: invalid status code: %llu\n", (unsigned long long)status_code);
		fflush(stderr);
		backend->state = BACKEND_ERROR;
		return 0;
	}

	// Create response with headers only
	HTTP_Response *res = http_response_alloc(stream->arena, (HTTP_Status)status_code);

	// Parse headers
	b32 is_chunked = 0;
	String8 headers_only = str8_skip(headers_section, status_line_end + 2);
	String8 remaining = headers_only;

	while(remaining.size > 0)
	{
		u64 line_end = str8_find_needle(remaining, 0, str8_lit("\r\n"), 0);
		String8 header_line = (line_end >= remaining.size) ? remaining : str8_prefix(remaining, line_end);
		remaining = (line_end >= remaining.size) ? str8_zero() : str8_skip(remaining, line_end + 2);

		if(header_line.size == 0)
		{
			break;
		}

		u64 colon = str8_find_needle(header_line, 0, str8_lit(":"), 0);
		if(colon < header_line.size)
		{
			String8 name = str8_prefix(header_line, colon);
			String8 value = str8_skip(header_line, colon + 1);

			// Trim leading whitespace from value
			while(value.size > 0 && (value.str[0] == ' ' || value.str[0] == '\t'))
			{
				value = str8_skip(value, 1);
			}

			// Check for chunked transfer encoding
			if(str8_match(name, str8_lit("Transfer-Encoding"), StringMatchFlag_CaseInsensitive) &&
			   str8_match(value, str8_lit("chunked"), StringMatchFlag_CaseInsensitive))
			{
				is_chunked = 1;
				backend->chunked_state = CHUNK_SIZE;
				// Don't add Transfer-Encoding to headers (HTTP/2 doesn't use it)
			}
			else if(str8_match(name, str8_lit("Content-Length"), StringMatchFlag_CaseInsensitive) && is_chunked)
			{
				// Don't add Content-Length if chunked (will calculate after decoding)
			}
			else
			{
				// Rewrite relative Location headers
				if(str8_match(name, str8_lit("Location"), StringMatchFlag_CaseInsensitive) && value.size > 0 &&
				   value.str[0] != '/' && !str8_match(str8_prefix(value, 4), str8_lit("http"), StringMatchFlag_CaseInsensitive))
				{
					value = str8f(stream->arena, "%S/%S", backend->path_prefix, value);
				}

				http_header_add(stream->arena, &res->headers, name, value);
			}
		}
	}

	// Set initial body (will grow as data arrives)
	if(is_chunked)
	{
		// Body will be in decoded_body_buffer (initially empty)
		res->body = str8_zero();
	}
	else
	{
		// Body is raw bytes after headers
		u64 body_offset = backend->response_header_end;
		u64 body_size = backend->response_size > body_offset ? backend->response_size - body_offset : 0;
		res->body = (String8){backend->response_buffer + body_offset, body_size};
	}

	stream->response = res;

	// Submit response to nghttp2 immediately
	h2_stream_send_response(session, stream);

	// Update state
	backend->state = BACKEND_HEADERS_PARSED;

	fprintf(stderr, "httpproxy: headers parsed OK (stream_id=%d, status=%llu, chunked=%d)\n", backend->stream_id,
	        (unsigned long long)status_code, is_chunked);
	fflush(stderr);

	return 1;
}

internal void
backend_decode_chunk_incremental(Backend_Connection *backend, H2_Session *session, Arena *arena)
{
	(void)session; // Currently unused, may be needed for future error handling

	// Start decoding from where headers end in raw buffer
	u64 raw_offset = backend->response_header_end;
	u8 *raw_data = backend->response_buffer + raw_offset;
	u64 raw_size = backend->response_size - raw_offset;

	// Ensure decode buffer exists
	if(backend->decoded_body_buffer == 0)
	{
		backend->decoded_body_capacity = 65536; // Start with 64KB
		backend->decoded_body_buffer = push_array(arena, u8, backend->decoded_body_capacity);
		backend->decoded_body_size = 0;
	}

	// State machine to decode chunks
	u64 parse_pos = 0; // Position in raw_data we're parsing from

	// Process available data through state machine
	while(parse_pos < raw_size && backend->chunked_state != CHUNK_DONE)
	{
		if(backend->chunked_state == CHUNK_SIZE)
		{
			// Find end of chunk size line
			String8 remaining = {raw_data + parse_pos, raw_size - parse_pos};
			u64 line_end = str8_find_needle(remaining, 0, str8_lit("\r\n"), 0);

			if(line_end >= remaining.size)
			{
				// Need more data to complete chunk size line
				return;
			}

			// Parse hex chunk size
			String8 size_str = str8_prefix(remaining, line_end);
			u64 chunk_size = 0;
			for(u64 i = 0; i < size_str.size; i++)
			{
				u8 c = size_str.str[i];
				if(c >= '0' && c <= '9')
				{
					chunk_size = (chunk_size << 4) | (c - '0');
				}
				else if(c >= 'a' && c <= 'f')
				{
					chunk_size = (chunk_size << 4) | (c - 'a' + 10);
				}
				else if(c >= 'A' && c <= 'F')
				{
					chunk_size = (chunk_size << 4) | (c - 'A' + 10);
				}
				else
				{
					break; // Stop at semicolon or whitespace
				}
			}

			parse_pos += line_end + 2; // Skip size line and \r\n

			if(chunk_size == 0)
			{
				// Final chunk
				backend->chunked_state = CHUNK_DONE;
				return;
			}

			// Transition to reading chunk data
			backend->chunk_remaining = chunk_size;
			backend->chunked_state = CHUNK_DATA;
		}
		else if(backend->chunked_state == CHUNK_DATA)
		{
			// Copy available chunk data
			u64 available = raw_size - parse_pos;
			u64 to_copy = (available < backend->chunk_remaining) ? available : backend->chunk_remaining;

			// Grow decode buffer if needed
			if(backend->decoded_body_size + to_copy > backend->decoded_body_capacity)
			{
				u64 new_capacity = backend->decoded_body_capacity * 2;
				while(backend->decoded_body_size + to_copy > new_capacity)
				{
					new_capacity *= 2;
				}
				u8 *new_buffer = push_array(arena, u8, new_capacity);
				MemoryCopy(new_buffer, backend->decoded_body_buffer, backend->decoded_body_size);
				backend->decoded_body_buffer = new_buffer;
				backend->decoded_body_capacity = new_capacity;
			}

			// Copy chunk data to decode buffer
			MemoryCopy(backend->decoded_body_buffer + backend->decoded_body_size, raw_data + parse_pos, to_copy);
			backend->decoded_body_size += to_copy;
			parse_pos += to_copy;
			backend->chunk_remaining -= to_copy;

			// Check if chunk complete
			if(backend->chunk_remaining == 0)
			{
				backend->chunked_state = CHUNK_TRAILER;
			}
			else
			{
				// Need more data to complete chunk
				return;
			}
		}
		else if(backend->chunked_state == CHUNK_TRAILER)
		{
			// Expect \r\n after chunk data
			if(raw_size - parse_pos < 2)
			{
				// Need more data
				return;
			}

			// Skip \r\n
			parse_pos += 2;
			backend->chunked_state = CHUNK_SIZE;
		}
	}
}

internal void
backend_handle_read(Backend_Connection *backend, H2_Session *session, Arena *arena)
{
	int fd = (int)backend->socket.u64[0];

	fprintf(stderr, "httpproxy: backend_handle_read (stream_id=%d, state=%d)\n", backend->stream_id, backend->state);
	fflush(stderr);

	// Don't read from finished backends
	if(backend->state == BACKEND_DONE || backend->state == BACKEND_ERROR)
	{
		return;
	}

	// Cache stream pointer if already streaming (avoids redundant hash lookups)
	H2_Stream *stream = 0;
	if(backend->state == BACKEND_HEADERS_PARSED)
	{
		stream = h2_stream_table_get(session->streams, backend->stream_id);

		// Backpressure: if buffer is full, pause reading
		if(stream != 0)
		{
			u64 buffered = backend->response_size - stream->response_body_offset;
			if(buffered >= 1048576) // 1MB limit
			{
				// Too much buffered data - pause reading until client catches up
				return;
			}
		}
	}

	// Grow buffer if needed
	if(backend->response_size >= backend->response_capacity)
	{
		u64 new_capacity = backend->response_capacity == 0 ? 65536 : backend->response_capacity * 2;
		u8 *new_buffer = push_array(arena, u8, new_capacity);
		if(backend->response_buffer != 0)
		{
			MemoryCopy(new_buffer, backend->response_buffer, backend->response_size);
		}
		backend->response_buffer = new_buffer;
		backend->response_capacity = new_capacity;
	}

	// Read more data
	u64 space = backend->response_capacity - backend->response_size;
	ssize_t bytes = read(fd, backend->response_buffer + backend->response_size, space);

	if(bytes < 0 && errno == EAGAIN)
	{
		fprintf(stderr, "httpproxy: WARNING: read() returned EAGAIN (stream_id=%d, state=%d)\n", backend->stream_id,
		        backend->state);
		fflush(stderr);
	}

	if(bytes > 0)
	{
		if(backend->time_first_byte == 0)
		{
			backend->time_first_byte = os_now_microseconds();
		}
		backend->response_size += bytes;

		// Try to parse headers if not yet parsed
		if(backend->response_header_end == 0)
		{
			backend_try_parse_headers(backend, session);
		}
		// If headers already parsed, update body for streaming
		if(backend->state == BACKEND_HEADERS_PARSED)
		{
			// Get stream pointer if not already cached
			if(stream == 0)
			{
				stream = h2_stream_table_get(session->streams, backend->stream_id);
			}

			// Use cached stream pointer (no redundant hash lookup)
			if(stream != 0 && stream->response != 0)
			{
				if(backend->chunked_state != CHUNK_NONE)
				{
					// Decode new chunk data
					backend_decode_chunk_incremental(backend, session, arena);
					// Update response body to point to decoded buffer
					stream->response->body = (String8){backend->decoded_body_buffer, backend->decoded_body_size};
				}
				else
				{
					// Update body to point to growing raw buffer
					u64 body_offset = backend->response_header_end;
					u64 body_size = backend->response_size - body_offset;
					stream->response->body = (String8){backend->response_buffer + body_offset, body_size};
				}

				// Resume data stream in case it was deferred
				fprintf(stderr, "httpproxy: calling nghttp2_session_resume_data (stream_id=%d, body_size=%llu)\n",
				        backend->stream_id, (unsigned long long)stream->response->body.size);
				fflush(stderr);
				nghttp2_session_resume_data(session->ng_session, backend->stream_id);

				// Flush immediately to trigger the data callback
				h2_session_flush(session);
			}
		}
	}
	else if(bytes == 0)
	{
		// EOF - response complete
		// If headers never arrived, this is an error
		if(backend->response_header_end == 0)
		{
			fprintf(stderr, "httpproxy: ERROR: EOF before headers (stream_id=%d)\n", backend->stream_id);
			fflush(stderr);
			backend->state = BACKEND_ERROR;
		}
		else if(backend->chunked_state == CHUNK_SIZE || backend->chunked_state == CHUNK_DATA)
		{
			// Incomplete chunked response
			fprintf(stderr, "httpproxy: ERROR: EOF mid-chunk (stream_id=%d, state=%d)\n", backend->stream_id,
			        backend->chunked_state);
			fflush(stderr);
			backend->state = BACKEND_ERROR;
		}
		else
		{
			// Normal completion
			backend->state = BACKEND_DONE;
		}
	}
	else if(errno != EAGAIN && errno != EWOULDBLOCK)
	{
		fprintf(stderr, "httpproxy: ERROR: read failed: %s (stream_id=%d)\n", strerror(errno), backend->stream_id);
		fflush(stderr);
		backend->state = BACKEND_ERROR;
	}
}
