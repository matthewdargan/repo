#ifndef H2_CORE_H
#define H2_CORE_H

////////////////////////////////
//~ Forward Declarations

typedef struct H2_Session H2_Session;
typedef struct H2_Stream H2_Stream;
typedef struct H2_StreamTable H2_StreamTable;
typedef struct Backend_Connection Backend_Connection;

////////////////////////////////
//~ H2 Session Types

typedef void (*H2_RequestHandler)(H2_Session *session, H2_Stream *stream, HTTP_Request *req, void *user_data);

struct H2_Session
{
	Arena *arena;
	nghttp2_session *ng_session;
	H2_StreamTable *streams;
	SSL *ssl;
	OS_Handle socket;
	b32 want_write;
	H2_RequestHandler request_handler;
	void *request_handler_data;

	// Active backend connections (no threading, just async I/O)
	Backend_Connection **backends;
	u64 backend_count;
	u64 backend_capacity;
};

////////////////////////////////
//~ H2 Session Functions

internal H2_Session *h2_session_alloc(Arena *arena, SSL *ssl, OS_Handle socket, H2_RequestHandler handler,
                                      void *handler_data);
internal void h2_session_send_settings(H2_Session *session);
internal void h2_session_flush(H2_Session *session);
internal void h2_session_release(H2_Session *session);

////////////////////////////////
//~ H2 Event Loop Functions

internal void h2_session_add_backend(H2_Session *session, Backend_Connection *backend);
internal void h2_session_remove_backend(H2_Session *session, u64 index);
internal void h2_session_run_event_loop(H2_Session *session);

#endif // H2_CORE_H
