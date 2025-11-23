# Monorepo

Systems programming monorepo centered on a custom C99 standard library. Built for Clang and Linux, managed with Nix/NixOS.

## Overview

This is a systems programming monorepo centered around a custom C99 standard library. The base layer provides arena allocators, length-prefixed strings (`String8`), and explicit integer types (`u8`, `u64`, `s64`) as alternatives to malloc/free, null-terminated strings, and standard C integer types.

Built on this foundation are additional components: a [9P protocol](https://9fans.github.io/plan9port/man/man9/intro.html) implementation, command-line utilities for 9P filesystems, and NixOS/Home Manager system configurations.

The codebase uses a layered architecture with explicit dependencies, unity builds for compilation, and namespace prefixes for code organization.

## Quick Start

Build and run with Nix:

```sh
# Build a tool
nix build .#9p
nix build .#9pfs

# Run directly
nix run .#9mount
nix run .#9pfs
```

Use `-L` flag to see full build output including compiler errors:

```sh
nix build .#9pfs -L
```

Development environment loads automatically via direnv, or manually with `nix develop`.

## Architecture

The codebase is organized into layers that depend on each other in a directed acyclic graph. Layers correspond with namespace prefixes - short identifiers (1-3 characters) followed by an underscore that identify which layer code belongs to.

**Layer dependency graph:**
```
base/     Custom standard library (no dependencies)
9p/       9P protocol implementation (depends on base/)
cmd/      Command-line tools (depend on base/ and 9p/)
```

**Unity builds:** Each layer contains `inc.h` (includes all header files) and `inc.c` (includes all implementation files). Applications include these to compile the entire layer at once, improving compilation speed and enabling whole-program optimization.

## Documentation

### For Developers

- **[CLAUDE.md](CLAUDE.md)** - Complete project context: architecture, layers, coding standards, development workflow

### Layer Documentation

- **[base/README.md](base/)** - Base layer API reference (coming soon)
- **[9p/README.md](9p/)** - 9P protocol implementation (coming soon)

### Tool Documentation

- **[cmd/9pfs/README.md](cmd/9pfs/README.md)** - 9P file server (disk-backed with arena tmp/)
- **[cmd/9mount/README.md](cmd/9mount/README.md)** - Mount 9P filesystems
- **[cmd/9bind/README.md](cmd/9bind/README.md)** - Bind mount directories
- **[cmd/9umount/README.md](cmd/9umount/README.md)** - Unmount 9P filesystems
- **[cmd/mount-9p/README.md](cmd/mount-9p/README.md)** - mount(8) helper for 9P filesystems
- **[cmd/9p/README.md](cmd/9p/README.md)** - 9P protocol inspection tool

### Conceptual Documentation

- **[docs/](docs/)** - Architecture, design patterns, protocols (coming soon)

## Project Structure

**Source code:**
```
base/           Custom standard library (arena allocators, strings, OS abstraction)
9p/             9P protocol implementation (client, server, message encoding)
cmd/            Command-line binaries (9mount, 9bind, 9pfs, etc.)
docs/           Architecture and design documentation
```

**Build and configuration:**
```
flake-parts/    Modular Nix flake configuration (shells, packages, pre-commit)
home/           Home Manager user environment configurations
nixos/          NixOS system configurations
packages/       Nix package definitions for each binary
```

## Coding Standards

When writing code, use base layer abstractions:

- **Arena allocators** (`arena_alloc`, `temp_begin`/`temp_end`) instead of malloc/free
- **Length-prefixed strings** (`String8`) instead of null-terminated `char*`
- **Explicit types** (`u8`, `u64`, `s64`, `f32`, etc.) instead of int/long/float
- **Unity builds** - add new files to the layer's `inc.h` (for headers) and `inc.c` (for implementation)
- **Namespace conventions** - prefix functions and types with the layer's namespace

Complete standards and layer descriptions are in **[CLAUDE.md](CLAUDE.md)**.
