////////////////////////////////
//~ HTTP API

internal HTTP_Method
http_method_from_str8(String8 string)
{
	HTTP_Method result = HTTP_Method_Unknown;
	if(str8_match(string, str8_lit("GET"), 0))
	{
		result = HTTP_Method_GET;
	}
	else if(str8_match(string, str8_lit("HEAD"), 0))
	{
		result = HTTP_Method_HEAD;
	}
	else if(str8_match(string, str8_lit("POST"), 0))
	{
		result = HTTP_Method_POST;
	}
	else if(str8_match(string, str8_lit("PUT"), 0))
	{
		result = HTTP_Method_PUT;
	}
	else if(str8_match(string, str8_lit("DELETE"), 0))
	{
		result = HTTP_Method_DELETE;
	}
	else if(str8_match(string, str8_lit("CONNECT"), 0))
	{
		result = HTTP_Method_CONNECT;
	}
	else if(str8_match(string, str8_lit("OPTIONS"), 0))
	{
		result = HTTP_Method_OPTIONS;
	}
	else if(str8_match(string, str8_lit("TRACE"), 0))
	{
		result = HTTP_Method_TRACE;
	}
	else if(str8_match(string, str8_lit("PATCH"), 0))
	{
		result = HTTP_Method_PATCH;
	}
	return result;
}

internal String8
str8_from_http_status(HTTP_Status status)
{
	String8 result = str8_zero();
	switch(status)
	{
		case HTTP_Status_100_Continue:
			result = str8_lit("Continue");
			break;
		case HTTP_Status_101_SwitchingProtocols:
			result = str8_lit("Switching Protocols");
			break;
		case HTTP_Status_103_EarlyHints:
			result = str8_lit("Early Hints");
			break;
		case HTTP_Status_200_OK:
			result = str8_lit("OK");
			break;
		case HTTP_Status_201_Created:
			result = str8_lit("Created");
			break;
		case HTTP_Status_202_Accepted:
			result = str8_lit("Accepted");
			break;
		case HTTP_Status_204_NoContent:
			result = str8_lit("No Content");
			break;
		case HTTP_Status_206_PartialContent:
			result = str8_lit("Partial Content");
			break;
		case HTTP_Status_301_MovedPermanently:
			result = str8_lit("Moved Permanently");
			break;
		case HTTP_Status_302_Found:
			result = str8_lit("Found");
			break;
		case HTTP_Status_303_SeeOther:
			result = str8_lit("See Other");
			break;
		case HTTP_Status_304_NotModified:
			result = str8_lit("Not Modified");
			break;
		case HTTP_Status_307_TemporaryRedirect:
			result = str8_lit("Temporary Redirect");
			break;
		case HTTP_Status_308_PermanentRedirect:
			result = str8_lit("Permanent Redirect");
			break;
		case HTTP_Status_400_BadRequest:
			result = str8_lit("Bad Request");
			break;
		case HTTP_Status_401_Unauthorized:
			result = str8_lit("Unauthorized");
			break;
		case HTTP_Status_403_Forbidden:
			result = str8_lit("Forbidden");
			break;
		case HTTP_Status_404_NotFound:
			result = str8_lit("Not Found");
			break;
		case HTTP_Status_405_MethodNotAllowed:
			result = str8_lit("Method Not Allowed");
			break;
		case HTTP_Status_408_RequestTimeout:
			result = str8_lit("Request Timeout");
			break;
		case HTTP_Status_409_Conflict:
			result = str8_lit("Conflict");
			break;
		case HTTP_Status_410_Gone:
			result = str8_lit("Gone");
			break;
		case HTTP_Status_412_PreconditionFailed:
			result = str8_lit("Precondition Failed");
			break;
		case HTTP_Status_413_PayloadTooLarge:
			result = str8_lit("Payload Too Large");
			break;
		case HTTP_Status_414_URITooLong:
			result = str8_lit("URI Too Long");
			break;
		case HTTP_Status_415_UnsupportedMediaType:
			result = str8_lit("Unsupported Media Type");
			break;
		case HTTP_Status_416_RangeNotSatisfiable:
			result = str8_lit("Range Not Satisfiable");
			break;
		case HTTP_Status_422_UnprocessableEntity:
			result = str8_lit("Unprocessable Entity");
			break;
		case HTTP_Status_429_TooManyRequests:
			result = str8_lit("Too Many Requests");
			break;
		case HTTP_Status_500_InternalServerError:
			result = str8_lit("Internal Server Error");
			break;
		case HTTP_Status_501_NotImplemented:
			result = str8_lit("Not Implemented");
			break;
		case HTTP_Status_502_BadGateway:
			result = str8_lit("Bad Gateway");
			break;
		case HTTP_Status_503_ServiceUnavailable:
			result = str8_lit("Service Unavailable");
			break;
		case HTTP_Status_504_GatewayTimeout:
			result = str8_lit("Gateway Timeout");
			break;
		default:
			result = str8_lit("Unknown");
			break;
	}
	return result;
}

internal String8
http_header_get(HTTP_HeaderList *list, String8 name)
{
	String8 result = str8_zero();
	for(u64 i = 0; i < list->count; i += 1)
	{
		if(str8_match(list->headers[i].name, name, StringMatchFlag_CaseInsensitive))
		{
			result = list->headers[i].value;
			break;
		}
	}
	return result;
}

internal void
http_header_add(Arena *arena, HTTP_HeaderList *list, String8 name, String8 value)
{
	if(list->headers == 0)
	{
		list->capacity = 8;
		list->headers = push_array(arena, HTTP_Header, list->capacity);
		list->count = 0;
	}
	else if(list->count >= list->capacity)
	{
		u64 new_capacity = list->capacity * 2;
		HTTP_Header *new_headers = push_array(arena, HTTP_Header, new_capacity);
		MemoryCopy(new_headers, list->headers, sizeof(HTTP_Header) * list->count);
		list->headers = new_headers;
		list->capacity = new_capacity;
	}

	list->headers[list->count].name = name;
	list->headers[list->count].value = value;
	list->count += 1;
}
