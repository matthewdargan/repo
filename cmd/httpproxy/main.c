// clang-format off
#include "base/inc.h"
#include "http/inc.h"
#include "base/inc.c"
#include "http/inc.c"
// clang-format on

////////////////////////////////
//~ Backend Configuration

typedef struct Backend Backend;
struct Backend
{
	String8 path_prefix;
	String8 backend_host;
	u16 backend_port;
};

typedef struct ProxyConfig ProxyConfig;
struct ProxyConfig
{
	Backend *backends;
	u64 backend_count;
	u16 listen_port;
};

global ProxyConfig *proxy_config = 0;

////////////////////////////////
//~ Worker Thread Pool

typedef struct Worker Worker;
struct Worker
{
	u64 id;
	Thread handle;
};

typedef struct WorkQueueNode WorkQueueNode;
struct WorkQueueNode
{
	WorkQueueNode *next;
	OS_Handle connection;
};

typedef struct WorkerPool WorkerPool;
struct WorkerPool
{
	b32 is_live;
	Semaphore semaphore;
	Mutex mutex;
	Arena *arena;
	WorkQueueNode *queue_first;
	WorkQueueNode *queue_last;
	WorkQueueNode *node_free_list;
	Worker *workers;
	u64 worker_count;
};

global WorkerPool *worker_pool = 0;

internal WorkQueueNode *
work_queue_node_alloc(WorkerPool *pool)
{
	WorkQueueNode *node = 0;
	MutexScope(pool->mutex)
	{
		node = pool->node_free_list;
		if(node != 0)
		{
			SLLStackPop(pool->node_free_list);
		}
		else
		{
			node = push_array_no_zero(pool->arena, WorkQueueNode, 1);
		}
	}
	MemoryZeroStruct(node);
	return node;
}

internal void
work_queue_node_release(WorkerPool *pool, WorkQueueNode *node)
{
	MutexScope(pool->mutex) { SLLStackPush(pool->node_free_list, node); }
}

internal void
work_queue_push(WorkerPool *pool, OS_Handle connection)
{
	WorkQueueNode *node = work_queue_node_alloc(pool);
	node->connection = connection;
	MutexScope(pool->mutex) { SLLQueuePush(pool->queue_first, pool->queue_last, node); }
	semaphore_drop(pool->semaphore);
}

internal OS_Handle
work_queue_pop(WorkerPool *pool)
{
	if(!semaphore_take(pool->semaphore, max_u64))
	{
		return os_handle_zero();
	}

	OS_Handle result = os_handle_zero();
	WorkQueueNode *node = 0;
	MutexScope(pool->mutex)
	{
		if(pool->queue_first != 0)
		{
			node = pool->queue_first;
			result = node->connection;
			SLLQueuePop(pool->queue_first, pool->queue_last);
		}
	}

	if(node != 0)
	{
		work_queue_node_release(pool, node);
	}

	return result;
}

////////////////////////////////
//~ Backend Selection

internal Backend *
find_backend_for_path(ProxyConfig *config, String8 path)
{
	Backend *result = 0;
	u64 longest_match = 0;

	for(u64 i = 0; i < config->backend_count; i += 1)
	{
		Backend *backend = &config->backends[i];
		if(backend->path_prefix.size <= path.size && backend->path_prefix.size > longest_match)
		{
			String8 path_prefix = str8_prefix(path, backend->path_prefix.size);
			if(str8_match(path_prefix, backend->path_prefix, 0))
			{
				result = backend;
				longest_match = backend->path_prefix.size;
			}
		}
	}

	return result;
}

////////////////////////////////
//~ HTTP Proxy Logic

internal void
send_error_response(OS_Handle socket, HTTP_Status status, String8 message)
{
	Temp scratch = scratch_begin(0, 0);
	HTTP_Response *res = http_response_alloc(scratch.arena, status);
	http_header_add(scratch.arena, &res->headers, str8_lit("Content-Type"), str8_lit("text/plain"));
	http_header_add(scratch.arena, &res->headers, str8_lit("Connection"), str8_lit("close"));

	if(message.size > 0)
	{
		res->body = message;
	}
	else
	{
		res->body = res->status_text;
	}

	String8 content_length_str = str8_from_u64(scratch.arena, res->body.size, 10, 0, 0);
	http_header_add(scratch.arena, &res->headers, str8_lit("Content-Length"), content_length_str);

	String8 response_data = http_response_serialize(scratch.arena, res);
	int fd = (int)socket.u64[0];
	write(fd, response_data.str, response_data.size);
	scratch_end(scratch);
}

internal void
proxy_to_backend(HTTP_Request *req, Backend *backend, OS_Handle client_socket)
{
	Temp scratch = scratch_begin(0, 0);

	OS_Handle backend_socket = os_socket_connect_tcp(backend->backend_host, backend->backend_port);
	if(os_handle_match(backend_socket, os_handle_zero()))
	{
		send_error_response(client_socket, HTTP_Status_502_BadGateway, str8_lit("Backend unavailable"));
		scratch_end(scratch);
		return;
	}

	int backend_fd = (int)backend_socket.u64[0];
	int client_fd = (int)client_socket.u64[0];

	String8 request_data = http_request_serialize(scratch.arena, req);
	write(backend_fd, request_data.str, request_data.size);

	u8 buffer[65536];
	for(;;)
	{
		ssize_t bytes = read(backend_fd, buffer, sizeof(buffer));
		if(bytes <= 0)
		{
			break;
		}
		write(client_fd, buffer, bytes);
	}

	os_file_close(backend_socket);
	scratch_end(scratch);
}

internal void
handle_http_request(HTTP_Request *req, OS_Handle client_socket, ProxyConfig *config)
{
	Temp scratch = scratch_begin(0, 0);

	Backend *backend = find_backend_for_path(config, req->path);
	if(backend == 0)
	{
		send_error_response(client_socket, HTTP_Status_404_NotFound, str8_lit("No backend configured for this path"));
		scratch_end(scratch);
		return;
	}

	proxy_to_backend(req, backend, client_socket);
	scratch_end(scratch);
}

////////////////////////////////
//~ Server Loop

internal void
handle_connection(OS_Handle connection_socket)
{
	Temp scratch = scratch_begin(0, 0);
	Arena *connection_arena = arena_alloc();
	Log *log = log_alloc();
	log_select(log);
	log_scope_begin();

	int client_fd = (int)connection_socket.u64[0];

	DateTime now = os_now_universal_time();
	String8 timestamp = str8_from_datetime(scratch.arena, now);
	log_infof("[%S] httpproxy: connection established\n", timestamp);

	u8 buffer[16384];
	ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer));
	if(bytes_read > 0)
	{
		String8 request_data = str8(buffer, (u64)bytes_read);
		HTTP_Request *req = http_request_parse(connection_arena, request_data);

		if(req->method != HTTP_Method_Unknown && req->path.size > 0)
		{
			log_infof("[%S] httpproxy: %S %S\n", timestamp, str8_from_http_method(req->method), req->path);
			handle_http_request(req, connection_socket, proxy_config);
		}
		else
		{
			send_error_response(connection_socket, HTTP_Status_400_BadRequest, str8_lit("Invalid HTTP request"));
		}
	}

	os_file_close(connection_socket);

	DateTime end_time = os_now_universal_time();
	String8 end_timestamp = str8_from_datetime(scratch.arena, end_time);
	log_infof("[%S] httpproxy: connection closed\n", end_timestamp);

	LogScopeResult result = log_scope_end(scratch.arena);
	if(result.strings[LogMsgKind_Info].size > 0)
	{
		fwrite(result.strings[LogMsgKind_Info].str, 1, result.strings[LogMsgKind_Info].size, stdout);
		fflush(stdout);
	}

	log_release(log);
	arena_release(connection_arena);
	scratch_end(scratch);
}

////////////////////////////////
//~ Worker Thread Entry Point

internal void
worker_thread_entry_point(void *ptr)
{
	WorkerPool *pool = (WorkerPool *)ptr;
	for(; pool->is_live;)
	{
		OS_Handle connection = work_queue_pop(pool);
		if(!os_handle_match(connection, os_handle_zero()))
		{
			handle_connection(connection);
		}
	}
}

////////////////////////////////
//~ Worker Pool Lifecycle

internal WorkerPool *
worker_pool_alloc(Arena *arena, u64 worker_count)
{
	WorkerPool *pool = push_array(arena, WorkerPool, 1);
	pool->arena = arena_alloc();

	pool->mutex = mutex_alloc();
	AssertAlways(pool->mutex.u64[0] != 0);

	pool->semaphore = semaphore_alloc(0, 1024, str8_zero());
	AssertAlways(pool->semaphore.u64[0] != 0);

	pool->worker_count = worker_count;
	pool->workers = push_array(arena, Worker, worker_count);

	return pool;
}

internal void
worker_pool_start(WorkerPool *pool)
{
	pool->is_live = 1;
	for(u64 i = 0; i < pool->worker_count; i += 1)
	{
		Worker *worker = &pool->workers[i];
		worker->id = i;
		worker->handle = thread_launch(worker_thread_entry_point, pool);
		AssertAlways(worker->handle.u64[0] != 0);
	}
}

internal void
worker_pool_shutdown(WorkerPool *pool)
{
	pool->is_live = 0;

	// Wake all workers so they can exit
	for(u64 i = 0; i < pool->worker_count; i += 1)
	{
		semaphore_drop(pool->semaphore);
	}

	// Wait for all workers to finish
	for(u64 i = 0; i < pool->worker_count; i += 1)
	{
		Worker *worker = &pool->workers[i];
		if(worker->handle.u64[0] != 0)
		{
			thread_join(worker->handle);
		}
	}

	semaphore_release(pool->semaphore);
	mutex_release(pool->mutex);
}

////////////////////////////////
//~ Entry Point

internal void
entry_point(CmdLine *cmd_line)
{
	Temp scratch = scratch_begin(0, 0);
	Arena *arena = arena_alloc();

	u64 worker_count = 0;
	String8 threads_str = cmd_line_string(cmd_line, str8_lit("threads"));
	if(threads_str.size > 0)
	{
		worker_count = u64_from_str8(threads_str, 10);
	}

	String8 port_str = cmd_line_string(cmd_line, str8_lit("port"));
	u16 listen_port = 8080;
	if(port_str.size > 0)
	{
		listen_port = (u16)u64_from_str8(port_str, 10);
	}

	proxy_config = push_array(arena, ProxyConfig, 1);
	proxy_config->listen_port = listen_port;
	proxy_config->backend_count = 1;
	proxy_config->backends = push_array(arena, Backend, 1);
	proxy_config->backends[0].path_prefix = str8_lit("/");
	proxy_config->backends[0].backend_host = str8_lit("127.0.0.1");
	proxy_config->backends[0].backend_port = 8000;

	fprintf(stdout, "httpproxy: listening on port %u\n", listen_port);
	fprintf(stdout, "httpproxy: proxying / to %.*s:%u\n", (int)proxy_config->backends[0].backend_host.size,
	        proxy_config->backends[0].backend_host.str, proxy_config->backends[0].backend_port);
	fflush(stdout);

	OS_Handle listen_socket = os_socket_listen_tcp(listen_port);
	if(os_handle_match(listen_socket, os_handle_zero()))
	{
		fprintf(stderr, "httpproxy: failed to listen on port %u\n", listen_port);
		fflush(stderr);
	}
	else
	{
		if(worker_count == 0)
		{
			u64 logical_cores = os_get_system_info()->logical_processor_count;
			worker_count = Max(4, logical_cores / 4);
		}

		worker_pool = worker_pool_alloc(arena, worker_count);
		worker_pool_start(worker_pool);

		fprintf(stdout, "httpproxy: launched %lu worker threads\n", (unsigned long)worker_count);
		fflush(stdout);

		for(;;)
		{
			OS_Handle connection_socket = os_socket_accept(listen_socket);
			if(os_handle_match(connection_socket, os_handle_zero()))
			{
				fprintf(stderr, "httpproxy: failed to accept connection\n");
				fflush(stderr);
				continue;
			}

			DateTime accept_time = os_now_universal_time();
			String8 accept_timestamp = str8_from_datetime(scratch.arena, accept_time);
			fprintf(stdout, "[%.*s] httpproxy: accepted connection\n", (int)accept_timestamp.size, accept_timestamp.str);
			fflush(stdout);

			work_queue_push(worker_pool, connection_socket);
		}
	}

	arena_release(arena);
	scratch_end(scratch);
}
