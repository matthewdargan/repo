# Monorepo

Systems programming monorepo centered on a custom C99 standard library. Built for Clang and Linux, managed with Nix/NixOS.

## Overview

Custom C99 standard library for systems programming. Layered architecture, unity builds, zero dependencies.

Layer README.md files document APIs (see [base/README.md](base/README.md)). [CLAUDE.md](CLAUDE.md) covers project structure and coding standards.

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

- **[base/README.md](base/README.md)** - Base layer API reference
- **[9p/README.md](9p/README.md)** - 9P protocol implementation

### Tool Documentation

- **[cmd/9pfs/README.md](cmd/9pfs/README.md)** - 9P file server (disk-backed with arena tmp/)
- **[cmd/9mount/README.md](cmd/9mount/README.md)** - Mount 9P filesystems
- **[cmd/9bind/README.md](cmd/9bind/README.md)** - Bind mount directories
- **[cmd/9umount/README.md](cmd/9umount/README.md)** - Unmount 9P filesystems
- **[cmd/mount-9p/README.md](cmd/mount-9p/README.md)** - mount(8) helper for 9P filesystems
- **[cmd/9p/README.md](cmd/9p/README.md)** - 9P protocol inspection tool

### Conceptual Documentation

- **[docs/architecture.md](docs/architecture.md)** - Layered architecture, compression-oriented programming, unity builds
- **[docs/9p-protocol.md](docs/9p-protocol.md)** - 9P protocol deep dive with wire format examples
- **[docs/9p-server.md](docs/9p-server.md)** - 9P server implementation
- **[docs/media-server.md](docs/media-server.md)** - Building a media server with simple tools (archived example)

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
