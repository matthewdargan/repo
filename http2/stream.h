#ifndef H2_STREAM_H
#define H2_STREAM_H

////////////////////////////////
//~ H2 Stream Types

struct H2_Stream
{
	s32 stream_id;
	Arena *arena;
	HTTP_Method method;
	String8 path;
	String8 query;
	HTTP_HeaderList headers;
	String8List body_chunks;
	u64 total_body_size;
	b32 headers_complete;
	b32 end_stream;

	Mutex response_mutex;
	HTTP_Response *response;
	b32 response_ready;
	u64 response_body_offset;
};

////////////////////////////////
//~ H2 Stream Table Types

read_only global u64 h2_stream_table_initial_capacity = 16;

typedef struct H2_StreamTableSlot H2_StreamTableSlot;
struct H2_StreamTableSlot
{
	s32 stream_id;
	H2_Stream *stream;
	b32 occupied;
};

struct H2_StreamTable
{
	Arena *arena;
	H2_StreamTableSlot *slots;
	u64 count;
	u64 capacity;
};

////////////////////////////////
//~ H2 Stream Functions

internal H2_Stream *h2_stream_alloc(Arena *arena, s32 stream_id);
internal HTTP_Request *h2_stream_to_request(Arena *arena, H2_Stream *stream);
internal void h2_stream_send_response(H2_Session *session, H2_Stream *stream);
internal ssize_t h2_data_source_read_callback(nghttp2_session *ng_session, int32_t stream_id, uint8_t *buf,
                                              size_t length, uint32_t *data_flags, nghttp2_data_source *source,
                                              void *user_data);

////////////////////////////////
//~ H2 Stream Table Functions

internal H2_StreamTable *h2_stream_table_alloc(Arena *arena);
internal void h2_stream_table_insert(H2_StreamTable *table, s32 stream_id, H2_Stream *stream);
internal H2_Stream *h2_stream_table_get(H2_StreamTable *table, s32 stream_id);
internal void h2_stream_table_remove(H2_StreamTable *table, s32 stream_id);

#endif // H2_STREAM_H
