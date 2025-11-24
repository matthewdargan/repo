# The 9P Protocol: Network File Systems Done Right

Network file systems are complicated. NFS has dozens of operations, complex state management, and hundreds of pages of specification. 9P is different. It has 14 operations, simple state, and you can understand the entire protocol in an afternoon.

9P was designed at Bell Labs for Plan 9, the operating system that came after Unix. The designers took everything they learned from Unix and NFS and distilled it into something simpler and more powerful.

## The Problem

You want to access files over a network. Traditional solutions like NFS work but have problems:

1. **Too many operations** - NFS v3 has 22 operations, v4 has 40+
2. **Complex state** - File handles, locks, caching protocols
3. **Platform-specific** - Assumes Unix semantics
4. **Hard to implement** - Thousands of lines of code

You need something simpler that still handles the common cases well.

## The Solution

9P provides 14 operations that cover all file system operations:

**Setup:**
- `Tversion/Rversion` - Negotiate protocol version
- `Tauth/Rauth` - Authenticate (optional)
- `Tattach/Rattach` - Attach to filesystem root

**File operations:**
- `Twalk/Rwalk` - Navigate directory hierarchy
- `Topen/Ropen` - Open file for reading/writing
- `Tcreate/Rcreate` - Create new file
- `Tread/Rread` - Read file data
- `Twrite/Rwrite` - Write file data
- `Tclunk/Rclunk` - Close file
- `Tremove/Rremove` - Delete file

**Metadata:**
- `Tstat/Rstat` - Get file information
- `Twstat/Rwstat` - Set file information

**Control:**
- `Tflush/Rflush` - Cancel in-flight request
- `Rerror` - Error response

Messages come in pairs. Client sends a T-message (transaction), server responds with R-message (reply). Every request gets exactly one response.

## Core Concepts

### Messages

All communication happens through messages. Each message has:

```
[4 bytes size][1 byte type][2 bytes tag][... message-specific data ...]
```

- **Size** - Total message length including this field (little-endian u32)
- **Type** - Message type code (u8)
- **Tag** - Request identifier for matching responses (little-endian u16)

The size field lets you read messages from a stream without parsing. Read 4 bytes, parse the size, read that many more bytes. You have a complete message.

Tags let you pipeline requests. Send multiple requests with different tags. Match responses to requests by tag. No need to wait for one response before sending the next request.

### Fids

A fid (file identifier) is a u32 that represents an open file or directory. The client picks fid numbers. The server tracks what each fid refers to.

Think of fids as file descriptors, but the client allocates them instead of the server. Client says "I'm going to use fid 1 to refer to a file" and the server remembers what fid 1 means for that client.

Fids start unassigned. You assign them with `Tattach` (mount root) or `Twalk` (navigate). You free them with `Tclunk` (close) or `Tremove` (delete).

### Qids

A qid (unique identifier) is a 13-byte structure that uniquely identifies a file:

```c
typedef struct Qid Qid;
struct Qid
{
	u32 type;      // File type (directory, regular file, etc.)
	u32 version;   // File version (changes on modification)
	u64 path;      // Unique file identifier
};
```

The server generates qids. The path must be unique across all files in the filesystem (use inode number or similar). The version increments when the file changes. The type indicates if it's a directory, file, etc.

Qids let clients detect when files change without reading metadata. Check the version—if it's different, the file changed.

## Wire Protocol

Let's walk through a complete session: connect, read a file, disconnect.

### 1. Version Negotiation

**Client sends Tversion:**
```
Size: 19 bytes
Type: 100 (Tversion)
Tag: 65535 (P9_TAG_NONE - no response needed for version)
Max message size: 8192
Protocol version: "9P2000" (6 bytes)

Bytes: [13 00 00 00] [64] [FF FF] [00 20 00 00] [06 00] [39 50 32 30 30 30]
       └─ 19        └─ 100 └─ tag └─ 8192     └─ len └─ "9P2000"
```

**Server responds Rversion:**
```
Size: 19 bytes
Type: 101 (Rversion)
Tag: 65535
Max message size: 8192 (may be smaller than requested)
Protocol version: "9P2000"

Bytes: [13 00 00 00] [65] [FF FF] [00 20 00 00] [06 00] [39 50 32 30 30 30]
```

Client and server agree on maximum message size and protocol version. If server doesn't understand "9P2000", it returns "unknown" and client disconnects.

### 2. Attach to Root

**Client sends Tattach:**
```
Size: 29 bytes
Type: 104 (Tattach)
Tag: 1
Fid: 1 (client picks this number)
Auth fid: 4294967295 (P9_FID_NONE - no authentication)
Username: "user" (4 bytes)
Attach path: "" (0 bytes - mount root)

Bytes: [1D 00 00 00] [68] [01 00] [01 00 00 00] [FF FF FF FF] [04 00] [75 73 65 72] [00 00]
       └─ 29        └─ 104 └─ tag └─ fid 1    └─ no auth   └─ len └─ "user"     └─ empty
```

**Server responds Rattach:**
```
Size: 20 bytes
Type: 105 (Rattach)
Tag: 1
Qid: {type: 0x80 (directory), version: 0, path: 1234}

Bytes: [14 00 00 00] [69] [01 00] [80] [00 00 00 00] [D2 04 00 00 00 00 00 00]
       └─ 20        └─ 105 └─ tag └─ dir └─ version └─ path 1234
```

Fid 1 now represents the root directory. The qid tells us it's a directory (type 0x80) with unique path 1234.

### 3. Walk to File

**Client sends Twalk:**
```
Size: 33 bytes
Type: 110 (Twalk)
Tag: 2
Fid: 1 (start from root)
New fid: 2 (will represent the target)
Name count: 2
Names: ["etc", "hosts"]

Bytes: [21 00 00 00] [6E] [02 00] [01 00 00 00] [02 00 00 00] [02 00]
       └─ 33        └─ 110 └─ tag └─ fid 1    └─ new fid 2 └─ 2 names
       [03 00] [65 74 63] [05 00] [68 6F 73 74 73]
       └─ len └─ "etc"   └─ len └─ "hosts"
```

Walk from fid 1 through "etc" then "hosts". If successful, fid 2 will represent `/etc/hosts`.

**Server responds Rwalk:**
```
Size: 33 bytes
Type: 111 (Rwalk)
Tag: 2
Qid count: 2
Qids: [{type: 0x80, version: 0, path: 5678},   // /etc
       {type: 0x00, version: 42, path: 9101}]  // /etc/hosts

Bytes: [21 00 00 00] [6F] [02 00] [02 00]
       └─ 33        └─ 111 └─ tag └─ 2 qids
       [80] [00 00 00 00] [8E 16 00 00 00 00 00 00]  // /etc qid
       [00] [2A 00 00 00] [8D 23 00 00 00 00 00 00]  // /etc/hosts qid
```

Server returns one qid for each path component. All exist, so fid 2 now represents `/etc/hosts`. Notice the file has version 42—if it changes, this increments.

### 4. Open File

**Client sends Topen:**
```
Size: 12 bytes
Type: 112 (Topen)
Tag: 3
Fid: 2
Mode: 0 (P9_OpenFlag_Read)

Bytes: [0C 00 00 00] [70] [03 00] [02 00 00 00] [00]
       └─ 12        └─ 112 └─ tag └─ fid 2    └─ read
```

**Server responds Ropen:**
```
Size: 24 bytes
Type: 113 (Ropen)
Tag: 3
Qid: {type: 0x00, version: 42, path: 9101}
IO unit: 8192 (optimal read/write size)

Bytes: [18 00 00 00] [71] [03 00] [00] [2A 00 00 00] [8D 23 00 00 00 00 00 00]
       └─ 24        └─ 113 └─ tag └─ file └─ vers └─ path
       [00 20 00 00]
       └─ iounit 8192
```

Fid 2 is now open for reading. IO unit tells client the optimal read size.

### 5. Read Data

**Client sends Tread:**
```
Size: 23 bytes
Type: 116 (Tread)
Tag: 4
Fid: 2
Offset: 0
Count: 8192

Bytes: [17 00 00 00] [74] [04 00] [02 00 00 00] [00 00 00 00 00 00 00 00]
       └─ 23        └─ 116 └─ tag └─ fid 2    └─ offset 0
       [00 20 00 00]
       └─ count 8192
```

**Server responds Rread:**
```
Size: 4 + 1 + 2 + 4 + <data length> bytes
Type: 117 (Rread)
Tag: 4
Count: <actual bytes read>
Data: <file contents>

Bytes: [... ...] [75] [04 00] [XX XX XX XX] [... file data ...]
       └─ size  └─ 117 └─ tag └─ count     └─ contents
```

Server reads from fid 2 at offset 0, returns up to 8192 bytes. If file is smaller, count reflects actual size. Client can issue more Tread messages with different offsets for large files.

### 6. Close File

**Client sends Tclunk:**
```
Size: 11 bytes
Type: 120 (Tclunk)
Tag: 5
Fid: 2

Bytes: [0B 00 00 00] [78] [05 00] [02 00 00 00]
       └─ 11        └─ 120 └─ tag └─ fid 2
```

**Server responds Rclunk:**
```
Size: 7 bytes
Type: 121 (Rclunk)
Tag: 5

Bytes: [07 00 00 00] [79] [05 00]
       └─ 7         └─ 121 └─ tag
```

Fid 2 is now free. Client can reuse that fid number for another file.

## Message Details

### Tversion / Rversion

Negotiate protocol version and maximum message size.

**Tversion:**
- `max_message_size` - Largest message client can handle
- `protocol_version` - Version string ("9P2000")

**Rversion:**
- `max_message_size` - Smallest of client/server max (negotiated size)
- `protocol_version` - "9P2000" if supported, "unknown" if not

Must be the first message. Uses tag `P9_TAG_NONE` (65535).

### Tattach / Rattach

Mount the filesystem root.

**Tattach:**
- `fid` - Fid to use for root
- `auth_fid` - Fid from successful Tauth, or `P9_FID_NONE`
- `user_name` - Username for access control
- `attach_path` - Path to mount (usually empty for root)

**Rattach:**
- `qid` - Qid of the root directory

After attach, the fid represents the root. Most implementations ignore `attach_path` and always mount at root.

### Twalk / Rwalk

Navigate the directory hierarchy.

**Twalk:**
- `fid` - Starting directory fid
- `new_fid` - Fid to assign to result (can be same as `fid`)
- `walk_name_count` - Number of path components (0-16)
- `walk_names` - Array of directory names

**Rwalk:**
- `walk_qid_count` - Number of qids returned
- `walk_qids` - Qid for each component walked

Walk doesn't open the file, just navigates. If any component doesn't exist, server returns qids for components that succeeded (may be zero). Check `walk_qid_count` matches `walk_name_count`.

Walk with zero names clones the fid—`new_fid` becomes a copy of `fid`.

### Topen / Ropen

Open a file for I/O.

**Topen:**
- `fid` - Fid to open (must be from walk or attach)
- `mode` - Open mode (read=0, write=1, rdwr=2, exec=3)

**Ropen:**
- `qid` - File qid
- `io_unit` - Optimal I/O size (0 means no limit)

After open, you can read/write the fid. Can't walk an opened fid—clone it first if you need to walk and keep it open.

### Tread / Rread

Read file data.

**Tread:**
- `fid` - Open fid
- `offset` - Byte offset to read from
- `count` - Maximum bytes to read

**Rread:**
- `count` - Actual bytes read
- `data` - File contents

Returns up to `count` bytes. May return less if file is smaller or EOF reached. Zero bytes means EOF.

For directories, returns encoded Dir9P structures (use `dir9p_decode`).

### Twrite / Rwrite

Write file data.

**Twrite:**
- `fid` - Open fid
- `offset` - Byte offset to write to
- `count` - Bytes to write
- `data` - File contents

**Rwrite:**
- `count` - Actual bytes written

Server must write all bytes or return error. Partial writes not allowed.

### Tclunk / Rclunk

Close a fid and free server resources.

**Tclunk:**
- `fid` - Fid to close

**Rclunk:**
- (no data)

After clunk, fid is invalid. Server can free any resources associated with it.

### Tremove / Rremove

Delete a file and clunk the fid.

**Tremove:**
- `fid` - Fid to remove

**Rremove:**
- (no data)

Removes the file and clunks the fid in one operation. If remove fails, fid remains valid.

### Tstat / Rstat

Get file metadata.

**Tstat:**
- `fid` - Fid to stat

**Rstat:**
- `stat_data` - Encoded Dir9P structure

Returns all file metadata (name, size, permissions, times, etc.). Use `dir9p_decode` to parse.

### Twstat / Rwstat

Set file metadata.

**Twstat:**
- `fid` - Fid to modify
- `stat_data` - Encoded Dir9P with fields to change

**Rwstat:**
- (no data)

Only specified fields are changed. Use length -1 (max_u64) to leave a field unchanged. Can rename files by changing the name field.

### Tflush / Rflush

Cancel an in-flight request.

**Tflush:**
- `tag` - Must use new tag
- `cancel_tag` - Tag of request to cancel

**Rflush:**
- (no data)

Used to cancel slow operations. Server must respond to Tflush after responding to the cancelled request (or Rerror if actually cancelled).

### Rerror

Error response (server to client only).

**Rerror:**
- `error_message` - Human-readable error string

Sent instead of normal R-message when operation fails. Client should display the message.

## Why This Works

9P's design makes it simple to implement and use:

1. **Request/response pairs** - Every T-message gets exactly one R-message. No complex state machines.

2. **Client-allocated fids** - Client picks fid numbers, so no "run out of file handles" errors. Client knows what fids are free.

3. **Qids for consistency** - Version field lets clients cache metadata and detect changes without polling.

4. **Pipelining with tags** - Send multiple requests without waiting. Server can process in any order.

5. **Simple wire format** - Size prefix makes parsing trivial. No complex framing.

6. **Stateless operations** - Each operation is self-contained. No "open file table" that gets out of sync.

7. **Works over any transport** - TCP, Unix sockets, pipes, serial connections. Just need reliable byte stream.

Compare to NFS:
- NFS v3: 22 operations, complex file handle encoding, separate mount protocol
- 9P: 14 operations, simple fid allocation, mount is just Tattach

The protocol is small enough to understand completely. You can implement a basic server in a few hundred lines of C.

## Implementation

This codebase provides complete 9P support:

**Protocol layer (9p/core.h):**
- `Message9P` - Protocol message structure
- `message9p_encode` - Serialize message to bytes
- `message9p_decode` - Parse bytes to message

**Client (9p/client.h):**
- `client9p_mount` - Connect and attach
- `client9p_open`, `client9p_create` - Open/create files
- `client9p_fid_pread`, `client9p_fid_pwrite` - Read/write
- `client9p_stat`, `client9p_wstat` - Metadata operations

**Server (9p/server.h):**
- `server9p_alloc` - Create server
- `server9p_get_request` - Get next request
- `server9p_respond` - Send response

**Filesystem (9p/fs.h):**
- `fs9p_open`, `fs9p_read`, `fs9p_write` - File operations
- `fs9p_opendir`, `fs9p_readdir` - Directory operations
- Dual storage: disk-backed + arena-based tmp/

See [9p/README.md](../9p/README.md) for API documentation and [cmd/9pfs/main.c](../cmd/9pfs/main.c) for a complete server implementation.

## Further Reading

**Protocol specification:**
- [intro(9) - Plan 9 File Protocol, 9P](https://9fans.github.io/plan9port/man/man9/intro.html) - The definitive reference

**This codebase:**
- [9p/README.md](../9p/README.md) - 9P layer API reference
- [docs/9p-server.md](9p-server.md) - Server implementation architecture
- [docs/architecture.md](architecture.md) - Layered architecture and design principles

**Tools:**
- [cmd/9p/README.md](../cmd/9p/README.md) - Command-line 9P client
- [cmd/9pfs/README.md](../cmd/9pfs/README.md) - 9P file server
- [cmd/9mount/README.md](../cmd/9mount/README.md) - Mount 9P filesystems
