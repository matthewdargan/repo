# Monorepo

C99 systems programming with custom standard library and 9P protocol implementation. Built for Clang and Linux, managed with Nix/NixOS.

## What is this?

A monorepo providing:

- **Custom standard library** - Arena allocators, length-prefixed strings, explicit types
- **9P protocol implementation** - Client and server for the [Plan 9 File Protocol](https://9fans.github.io/plan9port/man/man9/intro.html)
- **Command-line tools** - 9P mount utilities and file servers
- **System configuration** - NixOS and Home Manager configs

Replaces unsafe C stdlib patterns with safer abstractions: arena allocators instead of malloc/free, length-prefixed strings instead of null-terminated, explicit types instead of int/long.

## Quick Start

Build and run with Nix:

```sh
# Build a tool
nix build .#9p
nix build .#ramfs

# Run directly
nix run .#9mount
```

Use `-L` flag to see full build output including compiler errors:

```sh
nix build .#9p -L
```

Development environment loads automatically via direnv, or manually with `nix develop`.

## Architecture

Layered codebase with directed acyclic graph of dependencies. Each layer uses namespace prefixes to identify ownership.

```
base/     Custom standard library (foundation, no dependencies)
9p/       9P protocol implementation (depends on base/)
cmd/      Command-line tools (depend on base/ and 9p/)
```

**Unity builds:** Each layer has `inc.h` (includes all `.h`) and `inc.c` (includes all `.c`). Binaries include both to compile everything together.

## Documentation

### For Developers

- **[CLAUDE.md](CLAUDE.md)** - Complete project context: architecture, layers, coding standards, development workflow

### Layer Documentation

- **[base/README.md](base/)** - Base layer API reference (coming soon)
- **[9p/README.md](9p/)** - 9P protocol implementation (coming soon)

### Tool Documentation

- **[cmd/9mount/README.md](cmd/9mount/README.md)** - Mount 9P filesystems
- **[cmd/9bind/README.md](cmd/9bind/README.md)** - Bind 9P namespaces
- **[cmd/9umount/README.md](cmd/9umount/README.md)** - Unmount 9P filesystems
- **[cmd/9p/README.md](cmd/9p/README.md)** - 9P protocol tool
- **[cmd/ramfs/](cmd/ramfs/)** - In-memory 9P server (coming soon)

### Conceptual Documentation

- **[docs/](docs/)** - Architecture, design patterns, protocols (coming soon)

## Project Structure

```
base/           Custom standard library
9p/             9P protocol client and server
cmd/            Command-line binaries
packages/       Nix package definitions
flake-parts/    Modular Nix flake configuration
home/           Home Manager user configs
nixos/          NixOS system configs
docs/           Conceptual documentation
```

## Coding Standards

- Arena allocators over malloc/free
- `String8` over `char*`
- Explicit types (`u8`, `u64`, `s64`) over `int`/`long`
- Unity builds - add files to layer's `inc.h`/`inc.c`
- Follow namespace conventions

See **[CLAUDE.md](CLAUDE.md)** for complete standards.
