# Project Information

C89 monorepo with Nix/NixOS for all code and configuration.

## Overview

This is a systems programming monorepo implemented in strict C89 with modern tooling via Nix/NixOS. The project emphasizes zero-dependency, portable code with custom abstractions that prioritize safety and clarity over standard C conventions. The codebase prioritizes correctness, portability, and security through careful abstraction design while maintaining the simplicity and performance characteristics of C.

**Core Philosophy:**

- **Custom standard library (`libu/`)** - Replace unsafe C stdlib with arena allocators, length-based strings, and explicit type definitions
- **Unity builds** - Include `.c` files directly rather than separate compilation for simplicity and optimization
- **Zero dependencies** - Self-contained implementations using only system libraries
- **Nix-powered development** - Reproducible builds, development environments, and system configurations

**Key Features:**

- Custom memory management via arena allocators (no malloc/free)
- Length-prefixed strings (no null termination vulnerabilities)
- Network and file system utilities built on custom abstractions
- Consistent C89 codebase with modern tooling (clang-format, pre-commit hooks, gdb/valgrind)
- Full system configuration management through NixOS and Home Manager

## Project Structure

### `libu/` - Standard Library

- **`arena.c/h`** - Arena allocators (`Arena` for general use, `Temp` for scope-local)
- **`string.c/h`** - Length-based strings
- **`cmd.c/h`** - Command-line parsing
- **`os.c/h`** - OS operations
- **`socket.c/h`** - Network operations
- **`u.c/h`** - Custom type definitions

### `cmd/` - Binaries

- `9bind/`, `9mount/`, `9umount/` - 9P utilities
- `9p/` - 9P protocol tool
- `mediasrv/` - Media server
- `tor/` - Tor utility

### `lib9p/` - 9P Protocol

- **`9pclient.c/h`** - 9P client
- **`fcall.c/h`** - Protocol message handling

### `packages/` - Nix Packages

Package definitions for cmd binaries and dependencies.

### `home/` - Home Manager

User environment configurations and modules.

### `nixos/` - System Configurations

Device-specific configs (desktop, server, router).

## Coding Standards

**Always use libu abstractions instead of standard C:**
- Arena allocators over malloc/free
- Length-based strings over null-terminated
- Custom types (u8, u64, s64) over standard int types
- Unity builds - include .c files directly instead of separate compilation

## Key Commands

- `nix develop` - Enter dev shell (gdb, valgrind, pre-commit hooks)
- `nix build` - Build packages
- `nix run` - Run packages
