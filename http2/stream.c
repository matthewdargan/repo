////////////////////////////////
//~ H2 Stream Functions

internal H2_Stream *
h2_stream_alloc(Arena *arena, s32 stream_id)
{
	H2_Stream *stream = push_array(arena, H2_Stream, 1);
	stream->stream_id = stream_id;
	stream->arena = arena_alloc();
	stream->method = HTTP_Method_Unknown;
	stream->response_mutex = mutex_alloc();
	stream->ref_mutex = mutex_alloc();
	stream->ref_count = 1; // Start with 1 reference (for the stream table)
	stream->marked_for_cleanup = 0;
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

	if(res == 0 || stream->response_body_offset >= res->body.size)
	{
		*data_flags |= NGHTTP2_DATA_FLAG_EOF;
		return 0;
	}

	u64 remaining = res->body.size - stream->response_body_offset;
	u64 to_copy = remaining < length ? remaining : length;

	MemoryCopy(buf, res->body.str + stream->response_body_offset, to_copy);
	stream->response_body_offset += to_copy;

	if(stream->response_body_offset >= res->body.size)
	{
		*data_flags |= NGHTTP2_DATA_FLAG_EOF;
	}

	return (ssize_t)to_copy;
}

internal void
h2_stream_send_response(H2_Session *session, H2_Stream *stream)
{
	if(!stream->response_ready || stream->response == 0)
	{
		return;
	}

	MutexScope(stream->response_mutex)
	{
		HTTP_Response *res = stream->response;

		// Allocate from stream arena so data lives until stream cleanup
		String8 status_str = str8_from_u64(stream->arena, (u64)res->status_code, 10, 0, 0);

		u64 nv_count = 1 + res->headers.count;
		nghttp2_nv *nva = push_array(stream->arena, nghttp2_nv, nv_count);
		u64 nv_index = 0;

		nva[nv_index] = (nghttp2_nv){
		    (uint8_t *)":status", (uint8_t *)status_str.str, 7, status_str.size, NGHTTP2_NV_FLAG_NONE,
		};
		nv_index += 1;

		for(u64 i = 0; i < res->headers.count; i += 1)
		{
			nva[nv_index] = (nghttp2_nv){
			    (uint8_t *)res->headers.headers[i].name.str,
			    (uint8_t *)res->headers.headers[i].value.str,
			    res->headers.headers[i].name.size,
			    res->headers.headers[i].value.size,
			    NGHTTP2_NV_FLAG_NONE,
			};
			nv_index += 1;
		}

		// Protect nghttp2 session access with mutex (nghttp2 is not thread-safe)
		MutexScope(session->session_mutex)
		{
			if(res->body.size > 0)
			{
				nghttp2_data_provider data_prd;
				data_prd.source.ptr = stream;
				data_prd.read_callback = h2_data_source_read_callback;
				nghttp2_submit_response(session->ng_session, stream->stream_id, nva, nv_index, &data_prd);
			}
			else
			{
				nghttp2_submit_response(session->ng_session, stream->stream_id, nva, nv_index, 0);
			}
		}
	}
}

internal void
h2_stream_ref_inc(H2_Stream *stream)
{
	if(stream == 0)
	{
		return;
	}

	MutexScope(stream->ref_mutex) { stream->ref_count += 1; }
}

internal void
h2_stream_ref_dec(H2_Stream *stream, H2_StreamTable *table)
{
	if(stream == 0)
	{
		return;
	}

	b32 should_cleanup = 0;
	s32 stream_id = stream->stream_id;

	MutexScope(stream->ref_mutex)
	{
		if(stream->ref_count > 0)
		{
			stream->ref_count -= 1;
		}

		// If ref_count is 0 and stream is marked for cleanup, we can safely clean up
		if(stream->ref_count == 0 && stream->marked_for_cleanup)
		{
			should_cleanup = 1;
		}
	}

	if(should_cleanup)
	{
		arena_release(stream->arena);
		h2_stream_table_remove(table, stream_id);
	}
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
