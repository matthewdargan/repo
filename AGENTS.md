# Project Information

A systems programming monorepo implemented in C99 with Nix/NixOS. Built for Clang and Linux.

## Overview

This is a minimal-dependency C99 (ISO/IEC 9899:1999) codebase with a custom standard library. The project uses arena allocators, length-prefixed strings, and explicit types to replace unsafe C stdlib conventions. All code and system configurations are managed through Nix/NixOS.

## Architecture

The codebase is organized into layers. Layers are separated to isolate problems and allow selective inclusion in builds. Layers depend on other layers, but circular dependencies are not allowed - they form a directed acyclic graph.

Layers correspond with namespaces. Namespaces are short prefixes (1-3 characters) followed by an underscore, used to quickly identify which layer code belongs to. For example, `str8_` for string functions, `OS_` for operating system abstraction types.

**Core Design Principles:**

- **Custom standard library** - Arena allocators, length-prefixed strings, explicit types (u8, u64, s64, etc.)
- **Unity builds** - Each layer has `inc.h` (includes all layer `.h` files) and `inc.c` (includes all layer `.c` files), which are then included in `main.c`
- **Layered architecture** - Clear dependencies between layers, no circular dependencies
- **Namespace conventions** - Short prefixes identify layer ownership (e.g., `str8_`, `OS_`)
- **Minimal dependencies** - Self-contained implementations using only system libraries

## Layer Structure

### `base/` - Standard Library

The foundation layer. Provides custom implementations of common functionality without depending on any other project layers.

- **`context_cracking.h`** - Compiler, OS, architecture, and build option detection macros
- **`core.c/h`** - Types, macros, helper functions (u8, u64, s64, Min/Max, asserts, linked list macros)
- **`arena.c/h`** - Arena memory allocators (`Arena` for general use, `Temp` for scope-local)
- **`math.c/h`** - Vector and matrix math (Vec2, Vec3, Mat3x3, Mat4x4)
- **`string.c/h`** - Length-prefixed string operations (String8, conversions, formatting, lists)
- **`thread_context.c/h`** - Thread-local context management
- **`command_line.c/h`** - Command-line argument parsing
- **`os.c/h`** - Operating system abstraction (files, directories, memory, sockets, time)
- **`log.c/h`** - Logging utilities
- **`entry_point.c/h`** - Program entry point abstraction
- **`inc.h/inc.c`** - Unity build includes (all `.h` and `.c` files respectively)

### `9p/` - 9P Protocol

Implementation of the 9P protocol for network file systems.

- **`core.c/h`** - 9P protocol message encoding/decoding (Fcalls)
- **`dial.c/h`** - Dial string parsing and connection management
- **`client.c/h`** - 9P client implementation
- **`server.c/h`** - 9P server implementation (request handling, response generation)
- **`fs.c/h`** - Filesystem abstraction layer (path resolution, file operations, metadata, directory iteration)
- **`inc.h/inc.c`** - Unity build includes for the layer

### `auth/` - Authentication

Ed25519 and FIDO2 authentication for 9P protocol.

- **`ed25519.c/h`** - Ed25519 signature generation/verification
- **`fido2.c/h`** - FIDO2 hardware token support
- **`keyring.c/h`** - Credential storage and management
- **`rpc.c/h`** - Authentication RPC protocol
- **`fs.c/h`** - 9P filesystem interface for auth daemon
- **`inc.h/inc.c`** - Unity build includes for the layer

### `cmd/` - Command-line Tools

- **`9auth/`** - Authentication daemon (manages Ed25519/FIDO2 keys, provides auth for 9P)
- **`9auth-test/`** - Test program for 9auth authentication
- **`9pfs/`** - 9P file server (disk-backed, supports authentication)
- **`9pfs-test/`** - Test program for 9P file server implementations
- **`9mount/`** - FUSE-based 9P mount client (auto-reconnection, authentication)
- **`9p/`** - CLI tool for one-shot 9P operations
- **`authd/`** - Session authentication daemon for nginx auth_request

### `packages/` - Nix Packages

Nix package definitions for all cmd binaries. Each tool has a corresponding package that can be built with `nix build .#<name>`.

### `home/` - Home Manager

User environment configurations managed through Home Manager. Dotfiles, shell configuration, and per-user tooling.

### `nixos/` - System Configurations

Device-specific NixOS system configurations (desktop, server, router, etc.).

### `flake-parts/` - Nix Flake Modules

Modular flake configuration:
- **`cmd-package.nix`** - Package builder for cmd binaries
- **`shells.nix`** - Development shell definitions
- **`pre-commit.nix`** - Pre-commit hook configuration

## Coding Standards

When writing code, always use base layer abstractions:

- Arena allocators (`arena_alloc`, `temp_begin`/`temp_end`) instead of malloc/free
- Length-prefixed strings (`String8`) instead of null-terminated char*
- Explicit types (u8, u16, u32, u64, s8, s16, s32, s64, f32, f64, b32) instead of int/long/etc
- Unity builds - add new files to the layer's `inc.h` (for `.h` files) and `inc.c` (for `.c` files)
- Follow namespace conventions for the layer you're working in

## Development Environment

Development environment uses `direnv` with `.envrc`. Run `direnv allow` to load the development shell automatically when entering the repository. Development tools (clang, gdb, valgrind, pre-commit hooks) are automatically on PATH.

Pre-commit hooks run automatically on commit and enforce linting checks.

## Building and Running

**Build a package:**
```sh
nix build .#<package>
```

Examples: `nix build .#9p`, `nix build .#9pfs`, `nix build .#9mount`

**Build with full output (see compiler errors/warnings):**
```sh
nix build .#<package> -L
```

The `-L` flag prints the full build log. This is important for debugging build issues.

**Run a package directly:**
```sh
nix run .#<package>
```

Examples: `nix run .#9p`, `nix run .#9pfs`

## Testing

**Run all checks:**
```sh
nix flake check
```

This runs all automated tests defined in `flake-parts/checks.nix`.

**Run a specific check:**
```sh
nix build .#checks.x86_64-linux.9pfs-test -L
```

The `-L` flag shows full output, which is useful for debugging test failures.
