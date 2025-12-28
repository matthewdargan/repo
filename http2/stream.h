#ifndef H2_STREAM_H
#define H2_STREAM_H

////////////////////////////////
//~ Backend Connection State Machine

typedef enum Backend_State Backend_State;
enum Backend_State
{
	BACKEND_IDLE,
	BACKEND_CONNECTING,
	BACKEND_SENDING_REQUEST,
	BACKEND_READING_RESPONSE,
	BACKEND_HEADERS_PARSED, // Headers sent to client, streaming body
	BACKEND_DONE,
	BACKEND_ERROR,
};

typedef enum Chunked_State Chunked_State;
enum Chunked_State
{
	CHUNK_NONE,    // Not chunked encoding
	CHUNK_SIZE,    // Reading hex chunk size
	CHUNK_DATA,    // Reading chunk data
	CHUNK_TRAILER, // Reading trailing \r\n after chunk
	CHUNK_DONE,    // Final 0-size chunk received
};

typedef struct Backend_Connection Backend_Connection;
struct Backend_Connection
{
	// Hot path data - accessed frequently, keep at start for cache locality
	OS_Handle socket;        // 8 bytes - checked every event loop iteration
	u8 *response_buffer;     // 8 bytes - accessed on every read
	u64 response_size;       // 8 bytes - accessed on every read
	u64 response_capacity;   // 8 bytes - accessed on buffer growth
	u64 response_header_end; // 8 bytes - offset where headers end (0 = not found yet)
	u64 bytes_sent;          // 8 bytes - accessed on every write

	// String data (16 bytes each)
	String8 request_data; // 16 bytes - ptr + size
	String8 path_prefix;  // 16 bytes - ptr + size

	// Timing data (cold - only accessed on completion)
	u64 time_start;        // 8 bytes
	u64 time_connected;    // 8 bytes
	u64 time_request_sent; // 8 bytes
	u64 time_first_byte;   // 8 bytes

	// Small fields packed at end to minimize padding
	s32 stream_id;       // 4 bytes
	Backend_State state; // 4 bytes (enum)

	// Chunked encoding state (for incremental decoding)
	Chunked_State chunked_state; // 4 bytes (enum)
	u32 padding;                 // 4 bytes - explicit padding for alignment
	u64 chunk_remaining;         // 8 bytes - bytes left in current chunk
	u64 decoded_body_size;       // 8 bytes - total decoded body size
	u8 *decoded_body_buffer;     // 8 bytes - separate buffer for decoded body
	u64 decoded_body_capacity;   // 8 bytes - capacity of decode buffer
};

////////////////////////////////
//~ H2 Stream Types

struct H2_Stream
{
	s32 stream_id;
	Arena *arena;

	// Request data (filled by nghttp2 callbacks)
	HTTP_Method method;
	String8 path;
	String8 query;
	HTTP_HeaderList headers;
	String8List body_chunks;
	u64 total_body_size;
	b32 headers_complete;
	b32 end_stream;

	// Backend connection (if proxying)
	Backend_Connection *backend;

	// Response data (set when backend completes)
	HTTP_Response *response;
	u64 response_body_offset; // For nghttp2 data provider callback
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

////////////////////////////////
//~ Backend Connection Functions

internal Backend_Connection *backend_connection_alloc(Arena *arena, s32 stream_id);
internal void backend_connection_start(Backend_Connection *backend, String8 host, u16 port, String8 request_data);
internal void backend_handle_write(Backend_Connection *backend);
internal void backend_handle_read(Backend_Connection *backend, H2_Session *session, Arena *arena);
internal b32 backend_try_parse_headers(Backend_Connection *backend, H2_Session *session);
internal void backend_decode_chunk_incremental(Backend_Connection *backend, H2_Session *session, Arena *arena);

#endif // H2_STREAM_H
