# Layered Architecture: Building Code You Can Reason About

Most codebases grow into tangled messes. Files depend on other files which depend on configuration which depends on build system magic. You can't look at a piece of code and understand what it does without tracing through ten layers of abstraction.

This codebase is different. It's organized into layers that form a directed acyclic graph. Each layer has a clear purpose, explicit dependencies, and a namespace that tells you exactly where code belongs. You can reason about what code does by looking at it.

## The Problem

C doesn't have a module system. It has header files and source files and a preprocessor that copies text around. Most projects respond by adding build system complexity—CMake, autotools, pkg-config, and dozens of configuration files that tell the build system how to assemble everything.

This creates problems:

1. **Hidden dependencies** - You can't tell what a file depends on by looking at it
2. **Slow compilation** - Headers get parsed hundreds of times
3. **Unclear ownership** - Code scattered across directories with no organizing principle
4. **Poor inlining** - Functions across translation units don't get inlined
5. **Hard to understand** - Need to trace through build files to understand structure

Traditional approaches make it hard to reason about code. You spend time fighting the build system instead of understanding the program.

## The Solution: Layers

This codebase organizes code into layers. Layers are groups of related functionality that depend on other layers but never create circular dependencies. The dependency graph is a tree:

```
base/    - Custom standard library (no dependencies)
  ↑
9p/      - 9P protocol (depends on base/)
  ↑
cmd/     - Command-line tools (depend on base/ and 9p/)
```

Each layer:
- Has a clear purpose
- Depends only on layers below it
- Uses a namespace prefix to identify its code
- Compiles as a unity build

### Namespaces

Every layer has a namespace—a short prefix (1-3 characters) followed by an underscore. Function and type names start with this prefix. When you see a function call, you immediately know which layer owns it:

```c
String8 path = str8_lit("/tmp/file.txt");    // base/ layer (string operations)
Arena *arena = arena_alloc();                // base/ layer (memory)
OS_Handle file = os_file_open(...);          // base/ layer (OS abstraction)
Client9P *client = client9p_mount(...);      // 9p/ layer (9P client)
```

**base/ layer namespaces:**
- `str8_` - String operations
- `arena_` - Arena allocation
- `os_` - Operating system abstraction
- `thread_` - Threading primitives
- `log_` - Logging

**9p/ layer namespaces:**
- `client9p_` - 9P client
- `server9p_` - 9P server
- `fs9p_` - Filesystem abstraction
- `dial9p_` - Dial string parsing

This is [semantic compression](https://caseymuratori.com/blog_0015)—using short, consistent names that communicate meaning quickly. You don't need verbose names when the namespace tells you the context.

### Unity Builds

Each layer compiles as a unity build. Instead of compiling each `.c` file separately and linking them, we include all files in one translation unit:

**base/inc.h** (includes all headers):
```c
#ifndef INC_H
#define INC_H

#include "context_cracking.h"
#include "core.h"
#include "arena.h"
#include "math.h"
#include "string.h"
#include "thread.h"
#include "thread_context.h"
#include "command_line.h"
#include "os.h"
#include "log.h"
#include "entry_point.h"

#endif
```

**base/inc.c** (includes all implementations):
```c
#include "core.c"
#include "arena.c"
#include "math.c"
#include "string.c"
#include "thread.c"
#include "thread_context.c"
#include "command_line.c"
#include "os.c"
#include "log.c"
#include "entry_point.c"
```

A program includes both the headers and implementations:

```c
#include "base/inc.h"    // All layer headers
#include "9p/inc.h"
#include "base/inc.c"    // All layer implementations
#include "9p/inc.c"

// Your code here
```

This compiles the entire dependency tree in one translation unit. The compiler sees everything at once.

Benefits:

1. **Fast compilation** - Headers parsed once, not hundreds of times
2. **Better optimization** - Compiler can inline across the entire program
3. **Simple build** - One source file to compile, no linking step
4. **Clear dependencies** - Look at the includes to see what you depend on
5. **No hidden state** - Everything is `internal` (static) by default

### Layer Structure

Each layer follows the same structure:

```
base/
  ├── core.h / core.c           - Types, macros, helpers
  ├── arena.h / arena.c         - Arena allocators
  ├── string.h / string.c       - String operations
  ├── os.h / os.c               - OS abstraction
  └── inc.h / inc.c             - Unity build includes
```

The `inc.h` and `inc.c` files are the public interface. Users include these and get the entire layer.

## Memory Management: Arenas

The base layer replaces malloc/free with arena allocation. An arena is a large block of memory you allocate from by bumping a pointer. When you're done, you release the entire arena at once.

```c
Arena *arena = arena_alloc();
String8 *strings = push_array(arena, String8, 100);
// ... use strings ...
arena_release(arena);  // Free everything at once
```

For temporary allocations, use scratch arenas:

```c
internal void
process_file(String8 path)
{
    Temp scratch = scratch_begin(0, 0);

    // All allocations from scratch
    String8 contents = os_file_read(scratch.arena, ...);
    String8List lines = str8_split(scratch.arena, contents, str8_lit("\n"), 0);

    // Process lines...

    scratch_end(scratch);  // Automatic cleanup
}
```

No malloc. No free. No memory leaks. No use-after-free. Lifetimes are explicit and tied to scopes.

Ryan Fleury explains the arena approach in detail: [Untangling Lifetimes: The Arena Allocator](https://www.rfleury.com/p/untangling-lifetimes-the-arena-allocator)

## String Handling: Length-Prefixed Strings

The base layer replaces null-terminated `char*` with length-prefixed `String8`:

```c
typedef struct String8 String8;
struct String8
{
    u8 *str;
    u64 size;
};
```

You know the length without scanning. Substrings are pointer arithmetic. No buffer overflows from missing null terminators.

```c
String8 text = str8_lit("hello world");
String8 first = str8_prefix(text, 5);    // "hello" - no allocation
String8 rest = str8_skip(text, 6);       // "world" - no allocation
```

This is compression again—removing the implicit constraint (null terminator) makes the data structure simpler and faster.

## Explicit Types

No `int`, no `long`, no `size_t`. Every integer has an explicit size:

```c
u8, u16, u32, u64    - Unsigned integers
s8, s16, s32, s64    - Signed integers
f32, f64             - Floating point
b32                  - Boolean (32-bit for performance)
```

When you see `u64`, you know it's 64 bits on every platform. No surprises. No undefined behavior from integer promotion.

## How This Enables Reasoning

These decisions compound:

1. **Namespace prefixes** tell you what code does and where it lives
2. **Unity builds** let the compiler optimize across the entire program
3. **Arenas** make memory lifetimes explicit—no hidden allocation failures
4. **Length-prefixed strings** eliminate buffer overflows and strlen scanning
5. **Explicit types** remove platform-dependent behavior
6. **Layered dependencies** prevent tangled relationships

When you read code in this codebase, you can understand it. Function names tell you what layer owns them. Memory lifetimes are explicit. String operations can't overflow. Integer sizes don't change between platforms.

You can reason about what the code does by reading it.

## Adding New Layers

Want to add a layer? Create a directory with:

1. Header files (`.h`) for types and function declarations
2. Implementation files (`.c`) for function definitions
3. `inc.h` that includes all `.h` files
4. `inc.c` that includes all `.c` files
5. Use a short namespace prefix for all public symbols

Choose dependencies carefully. Only depend on layers below you in the graph. Never create cycles.

## Real-World Example: 9P File Server

The `cmd/9pfs` tool demonstrates how layers compose. It's a complete 9P file server in ~800 lines:

```c
// main.c
#include "base/inc.h"    // Memory, strings, OS
#include "9p/inc.h"      // 9P protocol
#include "base/inc.c"
#include "9p/inc.c"

// Server implementation uses:
// - base/ for arenas, strings, sockets, threads
// - 9p/ for protocol handling and filesystem abstraction
```

The server handles multiple clients concurrently, serves files from disk, provides an in-memory `tmp/` namespace, and handles all 9P protocol details. The layered architecture makes this manageable:

- `base/os.h` handles sockets and file I/O
- `base/thread.h` handles concurrency
- `9p/server.h` handles protocol messages
- `9p/fs.h` abstracts filesystem operations
- `cmd/9pfs/main.c` ties it together in 800 lines

Each layer solves its problem completely. The application composes them.

## Performance

This architecture is fast. Unity builds enable aggressive inlining and whole-program optimization. Arena allocation is faster than malloc (just pointer bumps, no metadata). Length-prefixed strings avoid strlen scans. Explicit types prevent hidden conversions.

The compiler sees the entire program. It can optimize across layers, inline small functions, and eliminate dead code. The generated code is often as good as hand-written assembly.

Use `valgrind` for memory profiling and leak detection. For CPU profiling, compile-time instrumented profiling is preferred—see [The RAD Debugger Project's base_profile.h](https://github.com/EpicGamesExt/raddebugger/blob/master/src/base/base_profile.h) for a reference implementation.

## Further Reading

**This codebase:**
- [base/README.md](../base/README.md) - Base layer API reference
- [9p/README.md](../9p/README.md) - 9P protocol layer API reference
- [CLAUDE.md](../CLAUDE.md) - Development workflow and coding standards

**Foundational concepts:**
- [Casey Muratori - Semantic Compression](https://caseymuratori.com/blog_0015)
- [Ryan Fleury - Untangling Lifetimes](https://www.rfleury.com/p/untangling-lifetimes-the-arena-allocator)
- [The RAD Debugger Project](https://github.com/EpicGamesExt/raddebugger)
