# 9auth

Authentication daemon for 9P. Manages Ed25519/FIDO2 credentials and provides challenge-response authentication via a 9P filesystem interface at `/run/9auth/socket`.

**auth-id binding**: Credentials are bound to (user, auth-id) pairs. A client connecting to server "nas" uses a different key than connecting to "router". This prevents key reuse across trust domains.

**Trust model**: Server imports client public keys. Only clients with imported keys can authenticate.

## Usage

```sh
9auth [options]
```

**Daemon mode** (no options): Loads keys from `/var/lib/9auth/keys`, listens on `/run/9auth/socket`.

**Key management mode**: Use `--register`, `--export`, or `--import` for credential operations.

## Options

**Key management:**
- `--register` - Generate new credential
- `--export` - Export public key
- `--import` - Import public key from stdin
- `--user=<user>` - Username
- `--auth-id=<id>` - Server identifier (matches server's `--auth-id`)
- `--proto=<ed25519|fido2>` - Protocol type

**Daemon configuration:**
- `--socket-path=<path>` - Socket location (default: `/run/9auth/socket`)
- `--keys-path=<path>` - Key storage (default: `/var/lib/9auth/keys`)
- `--threads=<n>` - Worker threads (default: max(4, cores/4))

## Examples

**Generate keypair:**
```sh
9auth --register --user=alice --auth-id=nas --proto=ed25519
```

**Export public key:**
```sh
9auth --export --user=alice --auth-id=nas > alice-nas.pubkey
```

**Import client public key (server side):**
```sh
9auth --import < alice-nas.pubkey
```

**FIDO2 hardware tokens:**
```sh
9auth --register --user=alice --auth-id=nas --proto=fido2
# Prompts for physical token touch
```

## Setup Workflow

**On each client:**
```sh
# Generate keypair and export public key
9auth --register --user=$(whoami) --auth-id=nas --proto=ed25519
9auth --export --user=$(whoami) --auth-id=nas > $(hostname)-nas.pubkey
```

**On server:**
```sh
# Import all client public keys
9auth --import < alice-nas.pubkey
9auth --import < bob-nas.pubkey
9auth --import < router-nas.pubkey

# Start authenticated server
9pfs --auth-id=nas --root=/media tcp!0.0.0.0!5640
```

**Client connects:**
```sh
9mount --auth-id=nas tcp!nas!5640 ~/media
```

## Security Properties

- **Key file permissions**: `/var/lib/9auth/keys` must be mode 0600 (daemon refuses to start otherwise)
- **Socket access**: `/run/9auth/socket` mode 0660, group `9auth` (NixOS module manages this)
- **Replay protection**: 10-second challenge window with timestamp validation
- **Challenge entropy**: 256-bit random + 64-bit timestamp

## 9P Filesystem Interface

9auth exposes a 9P filesystem for authentication:

- `rpc` - Read/write file for challenge-response state machine
- `ctl` - Control operations
- `log` - Audit log

The [`9pfs`](../9pfs/README.md) server, [`9mount`](../9mount/README.md) client, and [`9p`](../9p/README.md) CLI tool all connect to their local 9auth daemon via this interface.

### RPC Protocol

**Client side:**
```
write: "start role=client user=alice auth-id=nas"
read:  "challenge <base64>"
write: "response <base64-signature>"
read:  "done"
```

**Server side:**
```
write: "start role=server user=alice auth-id=nas"
write: "<base64-signature>"
read:  "ok" or "failed"
```

Auto-detects Ed25519 vs. FIDO2 based on stored credentials.
