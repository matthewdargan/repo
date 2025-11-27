# nix-cache - 9P-based Nix Binary Cache Server

A Nix binary cache server that serves cached build outputs over the 9P protocol instead of HTTP.

## Features

- **9P Protocol**: Serves Nix binary cache over 9P instead of HTTP
- **In-Memory Storage**: Uses temp9p for in-memory NAR and narinfo storage
- **Build Orchestration**: Automatically builds specified flake configurations
- **Multi-threaded**: Handles multiple concurrent 9P connections

## Usage

```bash
nix-cache [options] <address>
```

### Options

- `--flake=<path>` - Flake root directory (default: current directory)
- `--threads=<n>` - Number of worker threads (default: max(4, cores/4))

### Arguments

- `<address>` - Dial string (e.g., `tcp!*!9999` to listen on port 9999)

**Note:** nix-cache automatically discovers and builds **all** `nixosConfigurations` defined in the flake. No need to specify individual configs!

## Examples

### Basic Usage

Start a cache server on port 9999 (auto-discovers all configurations):

```bash
nix run .#nix-cache -- 'tcp!*!9999'
```

### Mounting the Cache

Once the server is running, you can mount it using 9mount:

```bash
9mount 'tcp!localhost!9999' /mnt/nix-cache
```

Then browse the cache:

```bash
ls /mnt/nix-cache/
# nix-cache-info  <hash1>.narinfo  <hash2>.narinfo  nar/
```

### Using with Nix

Configure Nix to use the 9P cache by mounting it and adding it as a substituter:

```nix
# In your configuration.nix or similar
nix.settings.substituters = [ "file:///mnt/nix-cache" ];
```

## Architecture

The server implements the Nix binary cache protocol with the following components:

1. **nix-cache-info** - Cache metadata file
2. **<hash>.narinfo** - Metadata for each store path (URL, compression, hashes, references)
3. **nar/<hash>.nar.xz** - Compressed NAR archives

### Filesystem Layout

```
/
  nix-cache-info          # Cache metadata
  trigger                 # Hot-reload trigger file
  configs/                # Per-config metadata
    <name>/
      generation          # Generation number
      path                # Store path
      timestamp           # Build timestamp
  <hash1>.narinfo         # Store path metadata
  <hash2>.narinfo
  nar/                    # NAR archives directory
    <hash1>.nar.xz        # Compressed NAR file
    <hash2>.nar.xz
```

All files are stored in-memory using the temp9p filesystem.

## Build Process

On startup, the server automatically:

1. Discovers all `nixosConfigurations` in the flake
2. Builds each configuration using the Nix C++ API
3. Generates compressed NAR archives (xz compression level 9)
4. Queries NAR hash and references for each store path
5. Generates narinfo metadata files
6. Stores everything in the temp9p in-memory filesystem

No manual configuration needed - just point it at a flake and it handles the rest!

## Hot-Reload

Trigger a background rebuild without restarting the server:

```bash
9p read tcp!localhost!9999 trigger
```

The server spawns a detached thread to rebuild all configurations while continuing to serve requests.

## Deployment

This is designed to be deployed on a NAS or build server where:

- Multiple devices (router, nas, etc.) pull their configurations from the cache
- The cache automatically rebuilds and serves the latest configurations
- Devices can pull updates by simply mounting the 9P server

## Future Enhancements

- Persistent storage option (disk-backed instead of temp9p)
- Signature verification for narinfo files
- Garbage collection for old store paths
