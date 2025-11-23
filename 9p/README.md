# 9p - Protocol Implementation

[9P](https://9fans.github.io/plan9port/man/man9/intro.html) protocol implementation for network file systems. Client, server, and filesystem abstraction.

## Overview

9P2000 protocol for network filesystems. Build file servers, connect to remote filesystems, serve files over TCP or Unix sockets.

Features:
- Client for connecting to 9P servers
- Server for building custom file servers
- Filesystem abstraction with dual storage (disk + arena)
- Dial string parsing (tcp!host!port, unix!/path/to/socket)
- Protocol message encoding/decoding

Depends on `base/` only.

## Core Types

**Protocol:**
- `Qid` - Unique file identifier (type, version, path)
- `Dir9P` - File/directory metadata
- `Message9P` - 9P protocol message

**Client:**
- `Client9P` - Client connection
- `ClientFid9P` - File identifier

**Server:**
- `Server9P` - Server instance
- `ServerRequest9P` - Incoming request
- `ServerFid9P` - Server-side file identifier

**Filesystem:**
- `FsContext9P` - Filesystem context (root path, tmp arena)
- `FsHandle9P` - File handle (disk or arena-backed)
- `TempNode9P` - In-memory file/directory node

## Dial Strings

Network addresses use Plan 9 dial syntax.

**TCP:**
```c
dial9p_connect(arena, str8_lit("tcp!localhost!5640"), str8_lit("tcp"), str8_lit("9pfs"));
dial9p_connect(arena, str8_lit("tcp!192.168.1.100!564"), str8_lit("tcp"), str8_lit("9pfs"));
```

**Unix sockets:**
```c
dial9p_connect(arena, str8_lit("unix!/tmp/9p.sock"), str8_lit("tcp"), str8_lit("9pfs"));
```

**Server listen:**
```c
OS_Handle listener = dial9p_listen(str8_lit("tcp!0.0.0.0!5640"), str8_lit("tcp"), str8_lit("9pfs"));
OS_Handle conn = os_socket_accept(listener);
```

## Client Usage

Connect to 9P servers and perform file operations.

**Basic connection:**
```c
Temp scratch = scratch_begin(0, 0);

// Connect
OS_Handle socket = dial9p_connect(scratch.arena, str8_lit("tcp!server!564"),
                                   str8_lit("tcp"), str8_lit("9pfs"));
u64 fd = socket.u64[0];
Client9P *client = client9p_mount(scratch.arena, fd, str8_zero());

// Use client...

client9p_unmount(scratch.arena, client);
os_file_close(socket);
scratch_end(scratch);
```

**File operations:**
```c
// Create file
ClientFid9P *fid = client9p_create(arena, client, str8_lit("/path/to/file"),
                                    P9_OpenFlag_Write, 0644);

// Write data
String8 data = str8_lit("hello world");
client9p_fid_pwrite(arena, fid, data.str, data.size, 0);

// Read data
u8 buffer[1024];
s64 n = client9p_fid_pread(arena, fid, buffer, sizeof(buffer), 0);
String8 contents = str8(buffer, n);

// Close
client9p_fid_close(arena, fid);
```

**Directory operations:**
```c
// List directory
ClientFid9P *dir = client9p_open(arena, client, str8_lit("/"), P9_OpenFlag_Read);
DirList9P entries = client9p_fid_read_dirs(arena, dir);

for(DirNode9P *node = entries.first; node != 0; node = node->next)
{
    Dir9P entry = node->dir;
    // entry.name, entry.length, entry.mode, etc.
}

client9p_fid_close(arena, dir);
```

**Stat/wstat:**
```c
// Get file metadata
Dir9P stat = client9p_stat(arena, client, str8_lit("/path/to/file"));
// stat.name, stat.length, stat.qid, stat.mode, etc.

// Change metadata
Dir9P new_stat = dir9p_zero();
new_stat.name = str8_lit("newname");
client9p_wstat(arena, client, str8_lit("/path/to/file"), new_stat);
```

## Server Usage

Implement custom 9P file servers.

**Basic server loop:**
```c
Server9P *server = server9p_alloc(arena, connection_fd, connection_fd);

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
        case Msg9P_Tversion: handle_version(request); break;
        case Msg9P_Tattach:  handle_attach(request);  break;
        case Msg9P_Twalk:    handle_walk(request);    break;
        case Msg9P_Topen:    handle_open(request);    break;
        case Msg9P_Tread:    handle_read(request);    break;
        case Msg9P_Twrite:   handle_write(request);   break;
        case Msg9P_Tclunk:   handle_clunk(request);   break;
        default:
            server9p_respond(request, str8_lit("unsupported operation"));
            break;
    }
}
```

**Example handlers:**
```c
internal void
handle_version(ServerRequest9P *request)
{
    request->out_msg.max_message_size = request->in_msg.max_message_size;
    request->out_msg.protocol_version = request->in_msg.protocol_version;
    server9p_respond(request, str8_zero());
}

internal void
handle_read(ServerRequest9P *request)
{
    // Get file data from fid->auxiliary
    MyFile *file = request->fid->auxiliary;

    u64 offset = request->in_msg.file_offset;
    u64 count = request->in_msg.byte_count;

    // Return data
    request->out_msg.payload_data = str8(file->data + offset, count);
    request->out_msg.byte_count = count;
    server9p_respond(request, str8_zero());
}
```

## Filesystem Abstraction

High-level API for serving files from disk and memory.

**Initialize context:**
```c
FsContext9P *ctx = fs9p_context_alloc(arena, str8_lit("/path/to/root"),
                                       str8_zero(), /*readonly=*/0);
```

**File operations:**
```c
// Open file (disk-backed)
FsHandle9P *handle = fs9p_open(arena, ctx, str8_lit("file.txt"), P9_OpenFlag_Read);

// Read
String8 data = fs9p_read(arena, handle, /*offset=*/0, /*count=*/1024);

// Write
fs9p_write(handle, /*offset=*/0, str8_lit("new data"));

// Close
fs9p_close(handle);
```

**Directory operations:**
```c
// Open directory
DirIterator9P *iter = fs9p_opendir(arena, ctx, str8_lit("/"));

// Read entries
String8 dirdata = fs9p_readdir(arena, ctx, iter, /*offset=*/0, /*count=*/8192);

fs9p_closedir(iter);
```

**Dual storage (disk + arena):**
```c
// Disk-backed file
FsHandle9P *disk_file = fs9p_open(arena, ctx, str8_lit("data.txt"), P9_OpenFlag_Read);

// In-memory file (tmp/ namespace)
FsHandle9P *tmp_file = fs9p_open(arena, ctx, str8_lit("tmp/temp.txt"), P9_OpenFlag_Write);

// Writes to tmp/ stored in arena, cleared on server shutdown
fs9p_write(tmp_file, 0, str8_lit("temporary data"));
```

**Path operations:**
```c
String8 joined = fs9p_path_join(arena, str8_lit("/home"), str8_lit("user"));  // "/home/user"
String8 base = fs9p_basename(arena, str8_lit("/home/user/file.txt"));  // "file.txt"
String8 dir = fs9p_dirname(arena, str8_lit("/home/user/file.txt"));    // "/home/user"
```

## See Also

- `core.h` - Protocol message encoding/decoding
- `client.h` - Client implementation
- `server.h` - Server implementation
- `fs.h` - Filesystem abstraction
- `dial.h` - Dial string parsing

For complete server implementation example, see `cmd/9pfs/main.c`.
