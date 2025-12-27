#ifndef H2_CORE_H
#define H2_CORE_H

////////////////////////////////
//~ Forward Declarations

typedef struct H2_Session H2_Session;
typedef struct H2_Stream H2_Stream;
typedef struct H2_StreamTable H2_StreamTable;
typedef struct H2_StreamTask H2_StreamTask;

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
	Mutex session_mutex;
};

struct H2_StreamTask
{
	H2_Session *session;
	s32 stream_id;
};

////////////////////////////////
//~ H2 Session Functions

internal H2_Session *h2_session_alloc(Arena *arena, SSL *ssl, OS_Handle socket, H2_RequestHandler handler,
                                      void *handler_data);
internal void h2_session_send_settings(H2_Session *session);
internal void h2_session_flush(H2_Session *session);
internal void h2_session_send_ready_responses(H2_Session *session);
internal void h2_session_release(H2_Session *session);

////////////////////////////////
//~ H2 Stream Task Handler

internal void h2_stream_task_handler(void *params);

#endif // H2_CORE_H
