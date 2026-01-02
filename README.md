# Monorepo

Systems programming monorepo centered on a custom C99 standard library. Built for Clang and Linux, managed with Nix/NixOS.

## Overview

Custom C99 standard library for systems programming. Layered architecture, unity builds, zero dependencies. [CLAUDE.md](CLAUDE.md) covers project structure and coding standards.

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

Layered dependency graph:
```
base/     Custom standard library
9p/       9P protocol (depends on base/)
cmd/      Tools (depend on base/ and 9p/)
```

Unity builds: Each layer has `inc.h` and `inc.c` that include all files. Applications include these to compile entire layers at once.

## Documentation

- [CLAUDE.md](CLAUDE.md) - Project context and coding standards
- [cmd/*/README.md](cmd/) - Tool documentation

## Structure

```
base/           Standard library
9p/             9P protocol
cmd/            Tools
www/            Website
packages/       Nix packages
nixos/          System configurations
home/           User configurations
```
