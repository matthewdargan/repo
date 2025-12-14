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
	Mutex mutex;
	u64 request_count;
	String8 last_path;
	String8 last_query;
	String8 last_method;
	Arena *arena;
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
		ssize_t bytes = read(fd, buffer, sizeof(buffer));

		if(bytes > 0)
		{
			Temp scratch = scratch_begin(&backend->arena, 1);
			String8 request_str = str8(buffer, (u64)bytes);
			HTTP_Request *req = http_request_parse(scratch.arena, request_str);

			if(req != 0)
			{
				MutexScope(backend->mutex)
				{
					backend->request_count += 1;
					backend->last_path = str8_copy(backend->arena, req->path);
					backend->last_query = str8_copy(backend->arena, req->query);
					backend->last_method = str8_copy(backend->arena, str8_from_http_method(req->method));
				}
			}

			scratch_end(scratch);
		}

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
	backend->arena = arena_alloc();
	backend->mutex = mutex_alloc();
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
//~ Test HTTP Proxy Server (Minimal Implementation)

typedef struct TestProxy TestProxy;
struct TestProxy
{
	Thread thread;
	OS_Handle listen_socket;
	u16 port;
	u16 backend_port;
	b32 is_live;
	b32 enable_acme_challenges;
	String8 acme_token;
	String8 acme_key_auth;
	Arena *arena;
};

internal void
test_proxy_thread(void *ptr)
{
	TestProxy *proxy = (TestProxy *)ptr;

	for(; proxy->is_live;)
	{
		OS_Handle connection_socket = os_socket_accept(proxy->listen_socket);
		if(os_handle_match(connection_socket, os_handle_zero()))
		{
			os_sleep_milliseconds(10);
			continue;
		}

		int fd = (int)connection_socket.u64[0];

		u8 buffer[4096];
		ssize_t bytes = read(fd, buffer, sizeof(buffer));

		if(bytes > 0)
		{
			Temp scratch = scratch_begin(&proxy->arena, 1);
			String8 request_str = str8(buffer, (u64)bytes);
			HTTP_Request *req = http_request_parse(scratch.arena, request_str);

			if(req != 0)
			{
				if(proxy->enable_acme_challenges && req->method == HTTP_Method_GET)
				{
					String8 acme_prefix = str8_lit("/.well-known/acme-challenge/");
					if(str8_match(str8_prefix(req->path, acme_prefix.size), acme_prefix, 0))
					{
						String8 token = str8_skip(req->path, acme_prefix.size);
						if(proxy->acme_token.size > 0 && str8_match(token, proxy->acme_token, 0))
						{
							String8 response = str8f(scratch.arena,
							                         "HTTP/1.1 200 OK\r\n"
							                         "Content-Type: text/plain\r\n"
							                         "Content-Length: %llu\r\n"
							                         "\r\n"
							                         "%S",
							                         proxy->acme_key_auth.size, proxy->acme_key_auth);
							write(fd, response.str, response.size);
							os_file_close(connection_socket);
							scratch_end(scratch);
							continue;
						}
					}
				}

				OS_Handle backend_socket = os_socket_connect_tcp(str8_lit("127.0.0.1"), proxy->backend_port);
				if(!os_handle_match(backend_socket, os_handle_zero()))
				{
					int backend_fd = (int)backend_socket.u64[0];
					write(backend_fd, buffer, bytes);

					u8 response_buffer[4096];
					ssize_t response_bytes = read(backend_fd, response_buffer, sizeof(response_buffer));
					if(response_bytes > 0)
					{
						write(fd, response_buffer, response_bytes);
					}

					os_file_close(backend_socket);
				}
			}

			scratch_end(scratch);
		}

		os_file_close(connection_socket);
	}
}

internal TestProxy *
test_proxy_start(Arena *arena, u16 port, u16 backend_port)
{
	TestProxy *proxy = push_array(arena, TestProxy, 1);
	proxy->port = port;
	proxy->backend_port = backend_port;
	proxy->arena = arena_alloc();
	proxy->listen_socket = os_socket_listen_tcp(port);

	if(os_handle_match(proxy->listen_socket, os_handle_zero()))
	{
		return 0;
	}

	proxy->is_live = 1;
	proxy->thread = thread_launch(test_proxy_thread, proxy);

	return proxy;
}

internal void
test_proxy_stop(TestProxy *proxy)
{
	proxy->is_live = 0;
	OS_Handle dummy = os_socket_connect_tcp(str8_lit("127.0.0.1"), proxy->port);
	if(!os_handle_match(dummy, os_handle_zero()))
	{
		os_file_close(dummy);
	}
	os_sleep_milliseconds(100);
	os_file_close(proxy->listen_socket);
	thread_join(proxy->thread);
}

////////////////////////////////
//~ Test HTTP Client Helper

internal String8
http_client_get(Arena *arena, String8 host, u16 port, String8 path)
{
	OS_Handle socket = os_socket_connect_tcp(host, port);
	if(os_handle_match(socket, os_handle_zero()))
	{
		return str8_zero();
	}

	int fd = (int)socket.u64[0];

	String8 request = str8f(arena, "GET %S HTTP/1.1\r\nHost: %S\r\n\r\n", path, host);
	write(fd, request.str, request.size);

	u8 buffer[4096];
	ssize_t bytes = read(fd, buffer, sizeof(buffer));
	os_file_close(socket);

	if(bytes <= 0)
	{
		return str8_zero();
	}

	return str8_copy(arena, str8(buffer, (u64)bytes));
}

////////////////////////////////
//~ Test Functions

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

	b32 result = 1;
	result = result && str8_match(host, str8_lit("localhost"), 0);
	result = result && str8_match(user_agent, str8_lit("test"), 0);
	return result;
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
test_proxy_basic_forwarding(Arena *arena, TestProxy *proxy, TestBackend *backend)
{
	String8 response = http_client_get(arena, str8_lit("127.0.0.1"), proxy->port, str8_lit("/"));

	if(response.size == 0)
	{
		return 0;
	}

	if(str8_find_needle(response, 0, str8_lit("200 OK"), 0) >= response.size)
	{
		return 0;
	}
	if(str8_find_needle(response, 0, str8_lit("Hello from backend!"), 0) >= response.size)
	{
		return 0;
	}

	u64 count = 0;
	MutexScope(backend->mutex) { count = backend->request_count; }

	return count >= 1;
}

internal b32
test_proxy_path_forwarding(Arena *arena, TestProxy *proxy, TestBackend *backend)
{
	String8 test_path = str8_lit("/api/users/123");
	String8 response = http_client_get(arena, str8_lit("127.0.0.1"), proxy->port, test_path);

	if(response.size == 0)
	{
		return 0;
	}

	String8 last_path = str8_zero();
	MutexScope(backend->mutex) { last_path = backend->last_path; }

	return str8_match(last_path, test_path, 0);
}

internal b32
test_proxy_multiple_requests(Arena *arena, TestProxy *proxy, TestBackend *backend)
{
	u64 initial_count = 0;
	MutexScope(backend->mutex) { initial_count = backend->request_count; }

	for(u64 i = 0; i < 10; i += 1)
	{
		String8 response = http_client_get(arena, str8_lit("127.0.0.1"), proxy->port, str8_lit("/"));
		if(response.size == 0)
		{
			return 0;
		}
	}

	u64 final_count = 0;
	MutexScope(backend->mutex) { final_count = backend->request_count; }

	return final_count >= initial_count + 10;
}

internal b32
test_acme_challenge_response(Arena *arena, TestProxy *proxy)
{
	proxy->enable_acme_challenges = 1;
	proxy->acme_token = str8_lit("test-token-123");
	proxy->acme_key_auth = str8_lit("test-token-123.test-key-authorization");

	String8 challenge_path = str8_lit("/.well-known/acme-challenge/test-token-123");
	String8 response = http_client_get(arena, str8_lit("127.0.0.1"), proxy->port, challenge_path);

	if(response.size == 0)
	{
		return 0;
	}

	if(str8_find_needle(response, 0, str8_lit("200 OK"), 0) >= response.size)
	{
		return 0;
	}

	if(str8_find_needle(response, 0, proxy->acme_key_auth, 0) >= response.size)
	{
		return 0;
	}

	return 1;
}

internal b32
test_acme_challenge_wrong_token(Arena *arena, TestProxy *proxy)
{
	proxy->enable_acme_challenges = 1;
	proxy->acme_token = str8_lit("test-token-123");
	proxy->acme_key_auth = str8_lit("test-token-123.test-key-authorization");

	String8 challenge_path = str8_lit("/.well-known/acme-challenge/wrong-token");
	String8 response = http_client_get(arena, str8_lit("127.0.0.1"), proxy->port, challenge_path);

	return str8_find_needle(response, 0, str8_lit("Hello from backend!"), 0) < response.size;
}

typedef struct ConcurrentTestData ConcurrentTestData;
struct ConcurrentTestData
{
	TestProxy *proxy;
	b32 success;
};

internal void
concurrent_request_thread(void *ptr)
{
	ConcurrentTestData *td = (ConcurrentTestData *)ptr;
	Temp scratch = scratch_begin(0, 0);

	String8 response = http_client_get(scratch.arena, str8_lit("127.0.0.1"), td->proxy->port, str8_lit("/"));
	td->success = response.size > 0 && str8_find_needle(response, 0, str8_lit("200 OK"), 0) < response.size;

	scratch_end(scratch);
}

internal b32
test_concurrent_requests(TestProxy *proxy)
{
	ConcurrentTestData data[4];
	Thread threads[4];

	for(u64 i = 0; i < 4; i += 1)
	{
		data[i].proxy = proxy;
		data[i].success = 0;
	}

	for(u64 i = 0; i < 4; i += 1)
	{
		threads[i] = thread_launch(concurrent_request_thread, &data[i]);
	}

	for(u64 i = 0; i < 4; i += 1)
	{
		thread_join(threads[i]);
	}

	b32 all_success = 1;
	for(u64 i = 0; i < 4; i += 1)
	{
		if(!data[i].success)
		{
			all_success = 0;
			break;
		}
	}

	return all_success;
}

internal b32
test_query_string_preservation(Arena *arena, TestProxy *proxy, TestBackend *backend)
{
	String8 test_path = str8_lit("/api");
	String8 test_query = str8_lit("key=value&foo=bar");
	String8 full_path = str8f(arena, "%S?%S", test_path, test_query);
	String8 response = http_client_get(arena, str8_lit("127.0.0.1"), proxy->port, full_path);

	if(response.size == 0)
	{
		return 0;
	}

	String8 last_path = str8_zero();
	String8 last_query = str8_zero();
	MutexScope(backend->mutex)
	{
		last_path = backend->last_path;
		last_query = backend->last_query;
	}

	b32 result = 1;
	result = result && str8_match(last_path, test_path, 0);
	result = result && str8_match(last_query, test_query, 0);
	return result;
}

internal b32
test_http_method_preservation(Arena *arena, TestProxy *proxy, TestBackend *backend)
{
	OS_Handle socket = os_socket_connect_tcp(str8_lit("127.0.0.1"), proxy->port);
	if(os_handle_match(socket, os_handle_zero()))
	{
		return 0;
	}

	int fd = (int)socket.u64[0];
	String8 request = str8_lit("POST /api/data HTTP/1.1\r\nHost: test\r\n\r\n");
	write(fd, request.str, request.size);

	u8 buffer[4096];
	ssize_t bytes = read(fd, buffer, sizeof(buffer));
	os_file_close(socket);

	if(bytes <= 0)
	{
		return 0;
	}

	String8 last_method = str8_zero();
	MutexScope(backend->mutex) { last_method = backend->last_method; }

	return str8_match(last_method, str8_lit("POST"), 0);
}

internal b32
test_empty_acme_token(Arena *arena, TestProxy *proxy)
{
	proxy->enable_acme_challenges = 1;
	proxy->acme_token = str8_zero();
	proxy->acme_key_auth = str8_lit("key-auth");

	String8 challenge_path = str8_lit("/.well-known/acme-challenge/");
	String8 response = http_client_get(arena, str8_lit("127.0.0.1"), proxy->port, challenge_path);

	return str8_find_needle(response, 0, str8_lit("Hello from backend!"), 0) < response.size;
}

internal b32
test_malformed_request(Arena *arena, TestProxy *proxy)
{
	OS_Handle socket = os_socket_connect_tcp(str8_lit("127.0.0.1"), proxy->port);
	if(os_handle_match(socket, os_handle_zero()))
	{
		return 0;
	}

	int fd = (int)socket.u64[0];
	String8 malformed = str8_lit("INVALID REQUEST\r\n\r\n");
	write(fd, malformed.str, malformed.size);
	os_file_close(socket);

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

	TestBackend *backend = test_backend_start(arena, 8001);
	if(backend == 0)
	{
		log_error(str8_lit("FAIL: failed to start test backend\n"));
		failed += 1;
		log_infof("test: %llu passed, %llu failed\n", passed, failed);
		return;
	}

	TestProxy *proxy = test_proxy_start(arena, 8002, 8001);
	if(proxy == 0)
	{
		log_error(str8_lit("FAIL: failed to start test proxy\n"));
		test_backend_stop(backend);
		failed += 1;
		log_infof("test: %llu passed, %llu failed\n", passed, failed);
		return;
	}

	os_sleep_milliseconds(200);

	if(test_proxy_basic_forwarding(arena, proxy, backend))
	{
		log_info(str8_lit("PASS: proxy_basic_forwarding\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: proxy_basic_forwarding\n"));
		failed += 1;
	}

	if(test_proxy_path_forwarding(arena, proxy, backend))
	{
		log_info(str8_lit("PASS: proxy_path_forwarding\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: proxy_path_forwarding\n"));
		failed += 1;
	}

	if(test_proxy_multiple_requests(arena, proxy, backend))
	{
		log_info(str8_lit("PASS: proxy_multiple_requests\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: proxy_multiple_requests\n"));
		failed += 1;
	}

	if(test_acme_challenge_response(arena, proxy))
	{
		log_info(str8_lit("PASS: acme_challenge_response\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: acme_challenge_response\n"));
		failed += 1;
	}

	if(test_acme_challenge_wrong_token(arena, proxy))
	{
		log_info(str8_lit("PASS: acme_challenge_wrong_token\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: acme_challenge_wrong_token\n"));
		failed += 1;
	}

	if(test_concurrent_requests(proxy))
	{
		log_info(str8_lit("PASS: concurrent_requests\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: concurrent_requests\n"));
		failed += 1;
	}

	if(test_query_string_preservation(arena, proxy, backend))
	{
		log_info(str8_lit("PASS: query_string_preservation\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: query_string_preservation\n"));
		failed += 1;
	}

	if(test_http_method_preservation(arena, proxy, backend))
	{
		log_info(str8_lit("PASS: http_method_preservation\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: http_method_preservation\n"));
		failed += 1;
	}

	if(test_empty_acme_token(arena, proxy))
	{
		log_info(str8_lit("PASS: empty_acme_token\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: empty_acme_token\n"));
		failed += 1;
	}

	if(test_malformed_request(arena, proxy))
	{
		log_info(str8_lit("PASS: malformed_request\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: malformed_request\n"));
		failed += 1;
	}

	test_proxy_stop(proxy);
	test_backend_stop(backend);

	log_infof("test: %llu passed, %llu failed\n", passed, failed);
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
