// clang-format off
#include "base/inc.h"
#include "json/inc.h"
#include "http/inc.h"
#include "base/inc.c"
#include "json/inc.c"
#include "http/inc.c"
// clang-format on

////////////////////////////////
//~ Test Backend Server

typedef struct TestBackend TestBackend;
struct TestBackend
{
	Thread thread;
	OS_Handle listen_socket;
	u16 port;
	b32 is_live;
};

internal void
test_backend_thread(void *ptr)
{
	TestBackend *backend = (TestBackend *)ptr;

	for(; backend->is_live;)
	{
		OS_Handle connection_socket = os_socket_accept(backend->listen_socket);
		if(os_handle_match(connection_socket, os_handle_zero()))
		{
			os_sleep_milliseconds(10);
			continue;
		}

		int fd = (int)connection_socket.u64[0];

		u8 buffer[4096];
		read(fd, buffer, sizeof(buffer));

		String8 response = str8_lit("HTTP/1.1 200 OK\r\n"
		                            "Content-Type: text/plain\r\n"
		                            "Content-Length: 20\r\n"
		                            "\r\n"
		                            "Hello from backend!\n");

		write(fd, response.str, response.size);
		os_file_close(connection_socket);
	}
}

internal TestBackend *
test_backend_start(Arena *arena, u16 port)
{
	TestBackend *backend = push_array(arena, TestBackend, 1);
	backend->port = port;
	backend->listen_socket = os_socket_listen_tcp(port);

	if(os_handle_match(backend->listen_socket, os_handle_zero()))
	{
		return 0;
	}

	backend->is_live = 1;
	backend->thread = thread_launch(test_backend_thread, backend);

	return backend;
}

internal void
test_backend_stop(TestBackend *backend)
{
	backend->is_live = 0;
	OS_Handle dummy = os_socket_connect_tcp(str8_lit("127.0.0.1"), backend->port);
	if(!os_handle_match(dummy, os_handle_zero()))
	{
		os_file_close(dummy);
	}
	os_sleep_milliseconds(100);
	os_file_close(backend->listen_socket);
	thread_join(backend->thread);
}

////////////////////////////////
//~ Test HTTP Request/Response

internal b32
test_http_request_parse(Arena *arena)
{
	String8 request_data = str8_lit("GET /test?foo=bar HTTP/1.1\r\n"
	                                "Host: localhost\r\n"
	                                "User-Agent: test\r\n"
	                                "\r\n");

	HTTP_Request *req = http_request_parse(arena, request_data);

	if(req->method != HTTP_Method_GET)
	{
		return 0;
	}
	if(!str8_match(req->path, str8_lit("/test"), 0))
	{
		return 0;
	}
	if(!str8_match(req->query, str8_lit("foo=bar"), 0))
	{
		return 0;
	}
	if(req->headers.count != 2)
	{
		return 0;
	}

	String8 host = http_header_get(&req->headers, str8_lit("Host"));
	String8 user_agent = http_header_get(&req->headers, str8_lit("User-Agent"));
	if(!str8_match(host, str8_lit("localhost"), 0))
	{
		return 0;
	}
	if(!str8_match(user_agent, str8_lit("test"), 0))
	{
		return 0;
	}

	return 1;
}

internal b32
test_http_response_serialize(Arena *arena)
{
	HTTP_Response *res = http_response_alloc(arena, HTTP_Status_200_OK);
	http_header_add(arena, &res->headers, str8_lit("Content-Type"), str8_lit("text/plain"));
	res->body = str8_lit("test body");

	String8 serialized = http_response_serialize(arena, res);

	if(str8_find_needle(serialized, 0, str8_lit("HTTP/1.1 200 OK"), 0) >= serialized.size)
	{
		return 0;
	}
	if(str8_find_needle(serialized, 0, str8_lit("Content-Type: text/plain"), 0) >= serialized.size)
	{
		return 0;
	}
	if(str8_find_needle(serialized, 0, str8_lit("test body"), 0) >= serialized.size)
	{
		return 0;
	}

	return 1;
}

internal b32
test_http_method_conversion(void)
{
	if(http_method_from_str8(str8_lit("GET")) != HTTP_Method_GET)
	{
		return 0;
	}
	if(http_method_from_str8(str8_lit("POST")) != HTTP_Method_POST)
	{
		return 0;
	}
	if(!str8_match(str8_from_http_method(HTTP_Method_GET), str8_lit("GET"), 0))
	{
		return 0;
	}

	return 1;
}

////////////////////////////////
//~ Test HTTP Client

internal b32
test_http_client_get(u16 backend_port)
{
	OS_Handle socket = os_socket_connect_tcp(str8_lit("127.0.0.1"), backend_port);
	if(os_handle_match(socket, os_handle_zero()))
	{
		return 0;
	}

	int fd = (int)socket.u64[0];

	String8 request = str8_lit("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
	write(fd, request.str, request.size);

	u8 buffer[4096];
	ssize_t bytes = read(fd, buffer, sizeof(buffer));
	os_file_close(socket);

	if(bytes <= 0)
	{
		return 0;
	}

	String8 response = str8(buffer, (u64)bytes);
	if(str8_find_needle(response, 0, str8_lit("200 OK"), 0) >= response.size)
	{
		return 0;
	}
	if(str8_find_needle(response, 0, str8_lit("Hello from backend!"), 0) >= response.size)
	{
		return 0;
	}

	return 1;
}

////////////////////////////////
//~ Test Runner

internal void
run_tests(Arena *arena)
{
	u64 passed = 0;
	u64 failed = 0;

	if(test_http_request_parse(arena))
	{
		log_info(str8_lit("PASS: http_request_parse\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: http_request_parse\n"));
		failed += 1;
	}

	if(test_http_response_serialize(arena))
	{
		log_info(str8_lit("PASS: http_response_serialize\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: http_response_serialize\n"));
		failed += 1;
	}

	if(test_http_method_conversion())
	{
		log_info(str8_lit("PASS: http_method_conversion\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: http_method_conversion\n"));
		failed += 1;
	}

	TestBackend *backend = test_backend_start(arena, 8000);
	if(backend == 0)
	{
		log_error(str8_lit("Failed to start test backend\n"));
		return;
	}

	os_sleep_milliseconds(100);

	if(test_http_client_get(8000))
	{
		log_info(str8_lit("PASS: http_client_get\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: http_client_get\n"));
		failed += 1;
	}

	test_backend_stop(backend);

	log_infof("test: %u passed, %u failed\n", passed, failed);
}

////////////////////////////////
//~ Entry Point

internal void
entry_point(CmdLine *cmd_line)
{
	Temp scratch = scratch_begin(0, 0);
	Log *log = log_alloc();
	log_select(log);
	log_scope_begin();

	run_tests(scratch.arena);

	LogScopeResult result = log_scope_end(scratch.arena);
	if(result.strings[LogMsgKind_Info].size > 0)
	{
		fwrite(result.strings[LogMsgKind_Info].str, 1, result.strings[LogMsgKind_Info].size, stdout);
		fflush(stdout);
	}
	if(result.strings[LogMsgKind_Error].size > 0)
	{
		fwrite(result.strings[LogMsgKind_Error].str, 1, result.strings[LogMsgKind_Error].size, stderr);
		fflush(stderr);
	}
	scratch_end(scratch);
}
