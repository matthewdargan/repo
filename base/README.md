# base - Standard Library

Custom C99 standard library. Arena allocators, length-prefixed strings, OS abstraction.

## Overview

Replaces standard C library conventions with safer, simpler alternatives:

- Arena allocators instead of malloc/free
- Length-prefixed strings (`String8`) instead of null-terminated `char*`
- Explicit integer types (`u8`, `u64`, `s64`) instead of `int`, `long`
- OS abstraction for files, sockets, threads, time

No dependencies on other project layers.

## Core Types

**Memory:**
- `Arena` - Memory arena allocator
- `Temp` - Scoped temporary arena

**Strings:**
- `String8` - Length-prefixed string (not null-terminated)
- `String8List` - Linked list of strings

**Numbers:**
- `u8, u16, u32, u64` - Unsigned integers
- `s8, s16, s32, s64` - Signed integers
- `f32, f64` - Floating point
- `b32` - Boolean (32-bit)

**OS:**
- `OS_Handle` - Platform-independent file/socket handle
- `Thread` - Thread handle
- `Semaphore` - Semaphore handle

## Arena Allocation

No malloc, no free. Allocate from arenas, release entire arena at once.

**Persistent arena:**
```c
Arena *arena = arena_alloc();
u8 *buffer = push_array(arena, u8, 1024);
MyStruct *data = push_array(arena, MyStruct, 10);
arena_release(arena);  // Free everything at once
```

**Temporary arena:**
```c
Temp scratch = scratch_begin(0, 0);
String8 path = str8_copy(scratch.arena, input_path);
// ... use path ...
scratch_end(scratch);  // Free all temporary allocations
```

**Common pattern in functions:**
```c
internal void
process_data(String8 input)
{
    Temp scratch = scratch_begin(0, 0);

    // All allocations from scratch.arena
    String8 cleaned = clean_string(scratch.arena, input);
    String8List parts = str8_split(scratch.arena, cleaned, str8_lit(","), 0);

    // ... process parts ...

    scratch_end(scratch);  // Cleanup automatic
}
```

## String Operations

Length-prefixed strings. Know size without scanning. Not null-terminated.

**String literals:**
```c
String8 greeting = str8_lit("hello");  // Compile-time size
String8 empty = str8_zero();           // Empty string
```

**Building strings:**
```c
Arena *arena = arena_alloc();
String8 name = str8_lit("user");
String8 path = str8f(arena, "/home/%S/docs", name);  // Format with %S
String8 combined = str8_cat(arena, str8_lit("prefix"), path);
```

**String comparison:**
```c
if(str8_match(a, b, 0)) { }                                // Exact match
if(str8_match(a, b, StringMatchFlag_CaseInsensitive)) { }  // Case insensitive
```

**String slicing:**
```c
String8 full = str8_lit("hello world");
String8 first = str8_prefix(full, 5);      // "hello"
String8 rest = str8_skip(full, 6);         // "world"
String8 last = str8_postfix(full, 5);      // "world"
```

**Working with C strings:**
```c
String8 from_c = str8_cstring(some_char_ptr);
char *null_terminated = (char *)str8_copy(arena, my_string).str;  // Adds null terminator
```

## Explicit Types

No `int`, no `long`. Explicit sizes prevent platform surprises.

```c
u8 byte = 0xff;
u32 count = 100;
u64 large = 1000000;
s64 offset = -42;
f32 ratio = 0.5f;
b32 flag = 1;  // Boolean, but 32-bit for performance
```

## OS Abstraction

Platform-independent file and socket operations.

**File I/O:**
```c
OS_Handle file = os_file_open(OS_AccessFlag_Read, path);
String8 contents = os_file_read(arena, file, rng1u64(0, max_u64));
os_file_close(file);

// Write file
os_file_write(file, rng1u64(0, data.size), data);
```

**Directory operations:**
```c
String8 cwd = os_get_current_path(arena);
os_set_current_path(new_path);
b32 exists = os_file_exists(path);
```

**Sockets:**
```c
OS_Handle socket = os_socket_open(OS_SocketFlag_TCP);
os_socket_connect(socket, address, port);
u64 sent = os_socket_send(socket, data.str, data.size);
os_socket_close(socket);
```

## See Also

- `core.h` - Types, macros, assertions
- `arena.h` - Arena allocators
- `string.h` - String operations
- `os.h` - Operating system abstraction
- `math.h` - Vector and matrix math
- `thread_context.h` - Thread-local context
- `command_line.h` - Command-line parsing
- `log.h` - Logging utilities
- `entry_point.h` - Program entry point
