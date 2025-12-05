# 9P Server Implementation

The 9P server layer (`9p/server.h`, `9p/server.c`) handles protocol message processing, fid management, and request lifecycle. The implementation uses hash tables for fid/request lookup, per-request scratch arenas for memory management, and integrates with the filesystem abstraction layer (`9p/fs.h`).

See [docs/9p-protocol.md](9p-protocol.md) for protocol wire format and message types. This document covers server-specific architecture and design decisions.

## Architecture Overview

The server is organized into three main components:

**1. Server Core (`Server9P`)**
- Message I/O (read requests, write responses)
- Fid tracking via hash table
- Request tracking via hash table
- Arena-based allocation (no malloc/free)

**2. Request Processing (`ServerRequest9P`)**
- Per-request scratch arena (temporary allocations freed after response)
- Automatic fid lookup based on message type
- Error accumulation (`request->error`)
- Response generation

**3. Fid Management (`ServerFid9P`)**
- Client-allocated fid numbers (9P protocol requirement)
- Hash table for O(1) lookup
- Auxiliary pointer for application state (`void *auxiliary`)
- Lifecycle tied to Tattach/Twalk (alloc) and Tclunk/Tremove (free)

### Integration with Filesystem Layer

The server layer handles protocol mechanics. File operations delegate to `fs9p_*` functions:

```c
// Server handles protocol
internal void
srv_read(ServerRequest9P *request)
{
	FidAuxiliary9P *aux = get_fid_aux(request->server->arena, request->fid);

	// Delegate to filesystem layer
	String8 data = fs9p_read(request->scratch.arena, aux->handle,
	                         request->in_msg.file_offset,
	                         request->in_msg.byte_count);

	request->out_msg.payload_data = data;
	request->out_msg.byte_count = data.size;
	server9p_respond(request, str8_zero());
}
```

This separation lets you swap filesystem implementations (disk, memory, synthetic) without changing protocol handling.

## Server Lifecycle

### Allocation

```c
internal Server9P *
server9p_alloc(Arena *arena, u64 input_fd, u64 output_fd)
{
    Server9P *server = push_array(arena, Server9P, 1);
    server->arena = arena;
    server->input_fd = input_fd;
    server->output_fd = output_fd;
    server->max_message_size = P9_IOUNIT_DEFAULT + P9_MESSAGE_HEADER_SIZE;
    server->read_buffer = push_array(arena, u8, server->max_message_size);
    server->write_buffer = push_array(arena, u8, server->max_message_size);
    server->max_fid_count = 4096;
    server->fid_table = push_array(arena, ServerFid9P *, server->max_fid_count);
    server->max_request_count = 4096;
    server->request_table = push_array(arena, ServerRequest9P *, server->max_request_count);
    server->next_tag = 1;
    // Freelists initialized to 0 (no free nodes yet)
    return server;
}
```

**Design decisions:**

- **Arena allocation** - Server lifetime matches connection lifetime. Allocate everything from one arena, release when connection closes. No per-request malloc/free overhead.

- **Fixed-size hash tables** - 4096 buckets each for fids and requests. Supports thousands of open files and concurrent requests with minimal collision. Simple modulo hash (`fid % 4096`).

- **Freelists for reuse** - Requests, fids, and fid auxiliary structures are recycled via freelists instead of being continuously allocated. This prevents arena growth over long-running connections while maintaining fast allocation.

- **Separate I/O buffers** - Read and write buffers sized to `max_message_size`. Prevents buffer allocation on every message.

- **File descriptor pair** - `input_fd` and `output_fd` can be same (TCP socket) or different (stdin/stdout for inetd-style deployment).

### Request Loop

```c
// From cmd/9pfs/main.c
for(;;)
{
    ServerRequest9P *request = server9p_get_request(server);
    if(request == 0) break;  // Connection closed

    if(request->error.size > 0)
    {
        server9p_respond(request, request->error);
        continue;
    }

    switch(request->in_msg.type)
    {
        case Msg9P_Tversion: srv_version(request); break;
        case Msg9P_Tattach:  srv_attach(request);  break;
        case Msg9P_Twalk:    srv_walk(request);    break;
        case Msg9P_Topen:    srv_open(request);    break;
        case Msg9P_Tread:    srv_read(request);    break;
        // ... other operations
        default:
            server9p_respond(request, str8_lit("unsupported operation"));
            break;
    }
}
```

**Pattern:** `server9p_get_request()` blocks reading the next message, decodes it, allocates a request structure, looks up relevant fids, and returns a fully-prepared request. Handler functions just implement business logic and call `server9p_respond()`.

## Request Processing

### Get Request

```c
internal ServerRequest9P *
server9p_get_request(Server9P *server)
{
	Temp scratch = scratch_begin(&server->arena, 1);

	// Read size-prefixed message from input_fd
	String8 msg = read_9p_msg(scratch.arena, server->input_fd);
	if(msg.size == 0)
	{
		scratch_end(scratch);
		return 0;  // Connection closed
	}

	// Decode wire format to Message9P
	Message9P f = msg9p_from_str8(msg);
	if(f.type == 0)
	{
		scratch_end(scratch);
		return 0;  // Parse error
	}

	// Allocate request from freelist or arena (checks for duplicate tag)
	ServerRequest9P *request = server9p_request_alloc(server, f.tag);
	if(request == 0)
	{
		scratch_end(scratch);
		return 0;  // Duplicate tag - protocol error
	}

	request->in_msg = f;
	request->scratch = scratch;

	// Automatic fid lookup based on message type
	switch(f.type)
	{
		case Msg9P_Tattach:
			request->fid = server9p_fid_alloc(server, f.fid);
			if(request->fid == 0)
			{
				request->error = str8_lit("duplicate fid");
			}
			break;

		case Msg9P_Twalk:
			request->fid = server9p_fid_lookup(server, f.fid);
			if(request->fid == 0)
			{
				request->error = str8_lit("unknown fid");
			}
			else if(f.fid != f.new_fid)
			{
				request->new_fid = server9p_fid_alloc(server, f.new_fid);
				if(request->new_fid == 0)
				{
					request->error = str8_lit("duplicate fid");
				}
			}
			else
			{
				request->new_fid = request->fid;
			}
			break;

		case Msg9P_Topen:
		case Msg9P_Tread:
		case Msg9P_Twrite:
		// ... other operations that need existing fid
			request->fid = server9p_fid_lookup(server, f.fid);
			if(request->fid == 0)
			{
				request->error = str8_lit("unknown fid");
			}
			break;
	}

	return request;
}
```

**Key pattern: Pre-validate fids before handler runs**

By the time your handler sees the request, `request->fid` is already looked up and validated. If `request->error.size > 0`, something went wrong (duplicate tag, unknown fid, etc.). Respond with error immediately:

```c
if(request->error.size > 0)
{
    server9p_respond(request, request->error);
    continue;
}
```

This centralizes error handling. Handlers can assume `request->fid` is valid.

### Respond

```c
internal b32
server9p_respond(ServerRequest9P *request, String8 err)
{
	if(request->responded != 0) return 0;  // Already responded
	request->responded = 1;

	// Build response message
	request->out_msg.tag = request->in_msg.tag;
	request->out_msg.type = request->in_msg.type + 1;  // Tread=116 -> Rread=117

	if(err.size > 0)
	{
		request->out_msg.error_message = err;
		request->out_msg.type = Msg9P_Rerror;
	}

	// Encode to wire format
	String8 buf = str8_from_msg9p(request->scratch.arena, request->out_msg);

	// Write complete message to output_fd
	// ... write loop with EINTR handling ...

	// Remove from request tracking table and return to freelist
	server9p_request_remove(server, request->in_msg.tag);

	// Free per-request scratch arena
	scratch_end(request->scratch);

	// Return request to freelist for reuse
	request->hash_next = server->request_free_list;
	server->request_free_list = request;

	return total_num_bytes_written == total_num_bytes_to_write;
}
```

**Design decisions:**

- **Automatic Rerror generation** - Pass non-empty error string, get Rerror. Pass empty string, get normal R-message with incremented type code.

- **Response uniqueness** - `responded` flag prevents double-response bugs.

- **Scratch arena cleanup** - All temporary allocations from `request->scratch.arena` freed here. Handler doesn't need to track memory.

- **Tag cleanup** - Request removed from hash table after response sent. Tag can be reused by client for next request.

## Per-Request Scratch Arenas

Every request gets a temporary arena (`request->scratch`) freed when the response is sent. Use this for all temporary allocations:

```c
internal void
srv_walk(ServerRequest9P *request)
{
	// Temporary allocation - will be freed after respond
	PathResolution9P res = fs9p_resolve_path(request->scratch.arena,
	                                          fs_context,
	                                          current_path, name);

	Dir9P stat = fs9p_stat(request->scratch.arena, fs_context, res.absolute_path);

	// ... use res and stat ...

	server9p_respond(request, str8_zero());
	// res and stat freed here automatically
}
```

**Permanent allocations** (data that outlives the request) use `request->server->arena`:

```c
internal void
srv_attach(ServerRequest9P *request)
{
	// Permanent - fid lives until clunk
	request->fid->user_id = str8_copy(request->server->arena, f.user_name);

	// Temporary - only needed for this response
	Dir9P root_stat = fs9p_stat(request->scratch.arena, fs_context, str8_zero());

	request->fid->qid = root_stat.qid;
	server9p_respond(request, str8_zero());
}
```

**Why this works:** Response encoding (`str8_from_msg9p`) points directly into scratch-allocated memory. Write the response bytes, then free the entire scratch arena. No tracking individual allocations.

## Fid Management

### Hash Table Lookup

```c
internal ServerFid9P *
server9p_fid_lookup(Server9P *server, u32 fid)
{
    u32 hash = fid % server->max_fid_count;
    for(ServerFid9P *f = server->fid_table[hash]; f != 0; f = f->hash_next)
    {
        if(f->fid == fid)
        {
            return f;
        }
    }
    return 0;
}
```

Simple modulo hash with linear probing via `hash_next` linked list. With 4096 buckets, collisions are rare for typical workloads (< 100 open fids per connection).

### Allocation

```c
internal ServerFid9P *
server9p_fid_alloc(Server9P *server, u32 fid)
{
    u32 hash = fid % server->max_fid_count;

    // Check for duplicate
    for(ServerFid9P *check = server->fid_table[hash]; check != 0; check = check->hash_next)
    {
        if(check->fid == fid)
        {
            return 0;  // Already exists
        }
    }

    // Try to reuse from freelist, otherwise allocate from arena
    ServerFid9P *f = server->fid_free_list;
    if(f != 0)
    {
        server->fid_free_list = f->hash_next;
    }
    else
    {
        f = push_array(server->arena, ServerFid9P, 1);
    }

    MemoryZeroStruct(f);
    f->fid = fid;
    f->open_mode = P9_OPEN_MODE_NONE;
    f->server = server;

    // Insert at head of hash bucket
    f->hash_next = server->fid_table[hash];
    server->fid_table[hash] = f;
    server->fid_count += 1;

    return f;
}
```

**Client allocates fid numbers** - Protocol design. Client picks arbitrary u32 values. Server just tracks what each number means. This is why duplicate detection is necessary.

### Removal

```c
internal ServerFid9P *
server9p_fid_remove(Server9P *server, u32 fid)
{
    u32 hash = fid % server->max_fid_count;
    ServerFid9P **prev = &server->fid_table[hash];
    for(ServerFid9P *f = *prev; f != 0; prev = &f->hash_next, f = f->hash_next)
    {
        if(f->fid == fid)
        {
            *prev = f->hash_next;  // Unlink from chain
            server->fid_count -= 1;
            return f;
        }
    }
    return 0;
}
```

Called by Tclunk and Tremove handlers. Returns the fid structure so handler can clean up auxiliary data:

```c
internal void
srv_clunk(ServerRequest9P *request)
{
	FidAuxiliary9P *aux = get_fid_aux(request->server, request->fid);

	// Clean up and return auxiliary to freelist
	fid_aux_release(request->server, aux);

	// Remove from hash table and return fid to freelist
	ServerFid9P *fid = server9p_fid_remove(request->server, request->in_msg.fid);
	fid->hash_next = request->server->fid_free_list;
	request->server->fid_free_list = fid;

	server9p_respond(request, str8_zero());
}
```

**Note:** Fids and auxiliary structures are returned to freelists for reuse instead of being freed. This prevents arena growth over long connections while maintaining fast O(1) allocation. Memory is reclaimed when connection closes and entire server arena is released.

## Auxiliary Data Pattern

Each `ServerFid9P` has `void *auxiliary` for application-specific state. The `cmd/9pfs` implementation uses this to track file paths and handles:

```c
typedef struct FidAuxiliary9P FidAuxiliary9P;
struct FidAuxiliary9P
{
    FidAuxiliary9P *next;       // Freelist linkage
    u64 path_len;               // Length of path in path_buffer
    FsHandle9P *handle;         // Open file handle (after Topen)
    b32 has_dir_iter;           // Whether dir_iter is active
    u32 open_mode;              // P9_OpenFlag_Read, etc.
    String8 cached_dir_entries; // Cached directory listing
    DirIterator9P dir_iter;     // Embedded directory iterator
    u8 path_buffer[PATH_MAX];   // Fixed-size path storage
};

internal FidAuxiliary9P *
get_fid_aux(Server9P *server, ServerFid9P *fid)
{
    if(fid->auxiliary == 0)
    {
        // Try freelist first, then allocate from arena
        FidAuxiliary9P *aux = server->fid_aux_free_list;
        if(aux != 0)
        {
            server->fid_aux_free_list = aux->next;
        }
        else
        {
            aux = push_array_no_zero(server->arena, FidAuxiliary9P, 1);
        }
        MemoryZeroStruct(aux);
        fid->auxiliary = aux;
    }
    return (FidAuxiliary9P *)fid->auxiliary;
}
```

The auxiliary structure uses fixed-size buffers for path storage instead of arena-allocated strings, preventing unbounded arena growth. Directory iterators are embedded directly instead of being heap-allocated.

**Usage example:**

```c
internal void
srv_walk(ServerRequest9P *request)
{
	FidAuxiliary9P *from_aux = get_fid_aux(request->server, request->fid);

	// Walk from current path
	String8 current_path = str8(from_aux->path_buffer, from_aux->path_len);
	PathResolution9P res = fs9p_resolve_path(request->scratch.arena,
	                                          fs_context,
	                                          current_path, name);

	// Store new path in new fid using fixed-size buffer
	FidAuxiliary9P *new_aux = get_fid_aux(request->server, request->new_fid);
	Assert(res.absolute_path.size <= sizeof(new_aux->path_buffer));
	MemoryCopy(new_aux->path_buffer, res.absolute_path.str, res.absolute_path.size);
	new_aux->path_len = res.absolute_path.size;

	request->new_fid->qid = stat.qid;
	server9p_respond(request, str8_zero());
}
```

This pattern keeps protocol handling (server layer) separate from filesystem state (auxiliary data).

## Multi-Client Handling

The `cmd/9pfs` implementation uses a worker thread pool to handle multiple concurrent connections:

### Worker Pool Architecture

```c
typedef struct WorkerPool WorkerPool;
struct WorkerPool
{
    b32 is_live;
    Semaphore semaphore;           // Work notification
    pthread_mutex_t mutex;         // Queue protection
    Arena *arena;                  // Pool-lifetime allocations
    WorkQueueNode *queue_first;    // FIFO queue of connections
    WorkQueueNode *queue_last;
    WorkQueueNode *node_free_list; // Recycled queue nodes
    Worker *workers;
    u64 worker_count;
};
```

**Design:**
- **FIFO queue** - Connections processed in order accepted
- **Semaphore signaling** - Workers block on semaphore, wake when connection available
- **Node recycling** - Queue nodes allocated once, reused via free list (no malloc/free churn)
- **Mutex-protected queue** - Multiple workers popping, main thread pushing

### Worker Thread Entry Point

```c
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
```

Each worker:
1. Blocks on `work_queue_pop()` (waits for semaphore)
2. Gets connection socket from queue
3. Runs complete request loop for that connection
4. Connection closes, worker loops back to step 1

### Connection Handler

```c
internal void
handle_connection(OS_Handle connection_socket)
{
    Temp scratch = scratch_begin(0, 0);
    Arena *connection_arena = arena_alloc();
    u64 connection_fd = connection_socket.u64[0];

    // Per-connection server instance
    Server9P *server = server9p_alloc(connection_arena, connection_fd, connection_fd);

    // Process requests until connection closes
    for(;;)
    {
        ServerRequest9P *request = server9p_get_request(server);
        if(request == 0) break;

        if(request->error.size > 0)
        {
            server9p_respond(request, request->error);
            continue;
        }

        switch(request->in_msg.type)
        {
            case Msg9P_Tread:    srv_read(request);    break;
            case Msg9P_Twrite:   srv_write(request);   break;
            // ... all operations
        }
    }

    os_file_close(connection_socket);
    arena_release(connection_arena);  // Free entire connection arena
    scratch_end(scratch);
}
```

**One server per connection** - Each `Server9P` instance has its own fid/request hash tables, I/O buffers, freelists, and arena. No shared state between connections. Workers can process different connections simultaneously without locking.

**Arena cleanup** - When connection closes, `arena_release(connection_arena)` releases the entire arena (server structure, hash tables, all fids, all buffers, freelists). One deallocation for hundreds of allocations.

### Main Loop

```c
internal void
entry_point(CmdLine *cmd_line)
{
    Arena *arena = arena_alloc();

    // Global filesystem context (read-only, safe to share)
    fs_context = fs9p_context_alloc(arena, root_path, str8_zero(), readonly);

    OS_Handle listen_socket = dial9p_listen(address, str8_lit("tcp"), str8_lit("9pfs"));

    // Spawn worker pool
    u64 worker_count = Max(4, logical_cores / 4);
    worker_pool = worker_pool_alloc(arena, worker_count);
    worker_pool_start(worker_pool);

    // Accept loop (main thread)
    for(;;)
    {
        OS_Handle connection_socket = os_socket_accept(listen_socket);
        work_queue_push(worker_pool, connection_socket);
    }
}
```

Main thread accepts connections and enqueues them. Workers dequeue and handle. This scales to hundreds of concurrent clients without spawning threads per connection.

**Global fs_context** - Filesystem abstraction is thread-safe for read-only operations. Multiple workers can call `fs9p_read()`, `fs9p_stat()`, etc., concurrently. Writes are serialized by OS (POSIX file descriptor operations).

## Implementation Reference

**Complete server implementation:**
- [cmd/9pfs/main.c](../cmd/9pfs/main.c) - Production server with worker pool, logging, command-line args

**Server layer:**
- [9p/server.h](../9p/server.h) - Server types and API
- [9p/server.c](../9p/server.c) - Request processing, fid management, hash tables

**Filesystem abstraction:**
- [9p/fs.h](../9p/fs.h) - Filesystem operations (open, read, write, stat, path resolution)
- [9p/fs.c](../9p/fs.c) - Dual storage: disk-backed + arena-based tmp/

**Protocol:**
- [9p/core.h](../9p/core.h) - Message encoding/decoding
- [docs/9p-protocol.md](9p-protocol.md) - Wire format, message types, protocol flow

**Architecture:**
- [docs/architecture.md](architecture.md) - Layered architecture principles
- [9p/README.md](../9p/README.md) - 9P layer overview
