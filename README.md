# Monorepo

Systems programming monorepo centered on a custom C99 standard library. Built for Clang and Linux, managed with Nix/NixOS.

## Overview

Custom C99 standard library for systems programming. Layered architecture, unity builds, minimal dependencies. [CLAUDE.md](CLAUDE.md) covers project structure and coding standards.

The centerpiece is a **9P ecosystem**: authenticated network filesystems with automatic recovery.

## Quick Start

Build and run with Nix:

```sh
# Build a tool
nix build .#9p
nix build .#9pfs
nix build .#9mount
nix build .#9auth

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

Layered dependency graph:
```
base/     Custom standard library (arena allocators, strings, OS abstraction)
9p/       9P protocol (client, server, dial, filesystem)
auth/     Ed25519/FIDO2 authentication (keyring, RPC)
cmd/      Tools (9p, 9pfs, 9mount, 9auth)
```

Unity builds: Each layer has `inc.h` and `inc.c` that include all files. Applications include these to compile entire layers at once.

## 9P Ecosystem

Network filesystems with cryptographic authentication:

```
┌──────────┐                   ┌─────────┐
│  Client  │                   │  9auth  │ (client)
│ (9mount, │──auth-id=nas────▶ │ daemon  │ Signs challenge with PRIVATE key
│    9p)   │                   └─────────┘
└────┬─────┘
     │
     │ 9P protocol over network (authenticated)
     │
     ▼
┌──────────┐                   ┌─────────┐
│  Server  │                   │  9auth  │ (server)
│  (9pfs)  │◀───auth-id=nas────│ daemon  │ Verifies with imported PUBLIC key
└──────────┘                   └─────────┘
```

**How it works:**

1. **Setup:** Client generates keypair, exports public key → Server imports it
2. **Runtime:** Server challenges → Client signs → Server verifies with imported public key

**Key concept - auth-id binding:** Credentials are bound to (user, auth-id) pairs. Client connecting to "nas" uses different key than connecting to "router". Prevents key reuse across trust domains.

**Tools:**

- **[9auth](cmd/9auth/README.md)** - Daemon managing Ed25519/FIDO2 credentials
- **[9pfs](cmd/9pfs/README.md)** - File server exporting local directories
- **[9mount](cmd/9mount/README.md)** - FUSE client with auto-recovery
- **[9p](cmd/9p/README.md)** - CLI for one-shot operations

**Features:**

- Ed25519 or FIDO2 hardware token authentication
- Automatic connection recovery with exponential backoff
- Compression-oriented design (arena allocation, packed data structures)
- NixOS modules for production deployment

## Documentation

- [CLAUDE.md](CLAUDE.md) - Project context and coding standards
- [cmd/9auth/README.md](cmd/9auth/README.md) - Authentication daemon
- [cmd/9pfs/README.md](cmd/9pfs/README.md) - File server
- [cmd/9mount/README.md](cmd/9mount/README.md) - FUSE mount client
- [cmd/9p/README.md](cmd/9p/README.md) - CLI tool

## Structure

Dependency order (foundation → applications → deployment):

```
base/           Standard library (arena allocators, strings, OS abstraction)
http/           HTTP protocol (request/response parsing)
json/           JSON serialization
9p/             9P protocol (client, server, filesystem layer)
auth/           Ed25519/FIDO2 authentication (keyring, RPC, 9P interface)
cmd/            Command-line tools (9auth, 9pfs, 9mount, 9p, authd)
packages/       Nix package definitions
nixos/          NixOS system configurations
home/           Home Manager user configurations
www/            Website
```
