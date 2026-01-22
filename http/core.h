#ifndef HTTP_CORE_H
#define HTTP_CORE_H

////////////////////////////////
//~ HTTP Types

typedef enum HTTP_Method
{
  HTTP_Method_GET,
  HTTP_Method_HEAD,
  HTTP_Method_POST,
  HTTP_Method_PUT,
  HTTP_Method_DELETE,
  HTTP_Method_CONNECT,
  HTTP_Method_OPTIONS,
  HTTP_Method_TRACE,
  HTTP_Method_PATCH,
  HTTP_Method_Unknown,
} HTTP_Method;

typedef enum HTTP_Status
{
  HTTP_Status_100_Continue = 100,
  HTTP_Status_101_SwitchingProtocols = 101,
  HTTP_Status_103_EarlyHints = 103,

  HTTP_Status_200_OK = 200,
  HTTP_Status_201_Created = 201,
  HTTP_Status_202_Accepted = 202,
  HTTP_Status_204_NoContent = 204,
  HTTP_Status_206_PartialContent = 206,

  HTTP_Status_301_MovedPermanently = 301,
  HTTP_Status_302_Found = 302,
  HTTP_Status_303_SeeOther = 303,
  HTTP_Status_304_NotModified = 304,
  HTTP_Status_307_TemporaryRedirect = 307,
  HTTP_Status_308_PermanentRedirect = 308,

  HTTP_Status_400_BadRequest = 400,
  HTTP_Status_401_Unauthorized = 401,
  HTTP_Status_403_Forbidden = 403,
  HTTP_Status_404_NotFound = 404,
  HTTP_Status_405_MethodNotAllowed = 405,
  HTTP_Status_408_RequestTimeout = 408,
  HTTP_Status_409_Conflict = 409,
  HTTP_Status_410_Gone = 410,
  HTTP_Status_412_PreconditionFailed = 412,
  HTTP_Status_413_PayloadTooLarge = 413,
  HTTP_Status_414_URITooLong = 414,
  HTTP_Status_415_UnsupportedMediaType = 415,
  HTTP_Status_416_RangeNotSatisfiable = 416,
  HTTP_Status_422_UnprocessableEntity = 422,
  HTTP_Status_429_TooManyRequests = 429,

  HTTP_Status_500_InternalServerError = 500,
  HTTP_Status_501_NotImplemented = 501,
  HTTP_Status_502_BadGateway = 502,
  HTTP_Status_503_ServiceUnavailable = 503,
  HTTP_Status_504_GatewayTimeout = 504,
} HTTP_Status;

typedef struct HTTP_Header HTTP_Header;
struct HTTP_Header
{
  String8 name;
  String8 value;
};

typedef struct HTTP_HeaderList HTTP_HeaderList;
struct HTTP_HeaderList
{
  HTTP_Header *headers;
  u64 count;
  u64 capacity;
};

////////////////////////////////
//~ HTTP API

internal HTTP_Method http_method_from_str8(String8 string);
internal String8 str8_from_http_status(HTTP_Status status);

internal String8 http_header_get(HTTP_HeaderList list, String8 name);
internal void http_header_add(Arena *arena, HTTP_HeaderList *list, String8 name, String8 value);

#endif // HTTP_CORE_H
