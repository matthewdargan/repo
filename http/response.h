#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

////////////////////////////////
//~ HTTP Response Types

typedef struct HTTP_Response HTTP_Response;
struct HTTP_Response
{
	u64 status_code;
	String8 status_text;
	String8 version;
	HTTP_HeaderList headers;
	String8 body;
};

////////////////////////////////
//~ HTTP Response Building

internal HTTP_Response *http_response_alloc(Arena *arena, HTTP_Status status);
internal String8 http_response_serialize(Arena *arena, HTTP_Response *res);

#endif // HTTP_RESPONSE_H
