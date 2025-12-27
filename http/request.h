#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

////////////////////////////////
//~ HTTP Request

typedef struct HTTP_Request HTTP_Request;
struct HTTP_Request
{
	HTTP_Method method;
	String8 path;
	String8 query;
	String8 version;
	HTTP_HeaderList headers;
	String8 body;
};

internal HTTP_Request *http_request_parse(Arena *arena, String8 data);
internal String8 http_request_serialize(Arena *arena, HTTP_Request *req);

#endif // HTTP_REQUEST_H
