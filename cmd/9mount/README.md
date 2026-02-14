# 9mount

FUSE-based 9P client with automatic connection recovery. Mounts remote 9P servers as local filesystems, translates VFS operations into 9P protocol messages. Unlike kernel v9fs, runs in userspace and integrates with [`9auth`](../9auth/README.md) for Ed25519/FIDO2 authentication.

## Usage

```sh
9mount [options] <dial> <mtpt>
```

**Arguments:**
- `<dial>` - Server address (`tcp!nas!5640`, `unix!/tmp/9p.sock`)
- `<mtpt>` - Local mount point (`/mnt/media`, `~/n/nas`)

**Options:**
- `--auth-daemon=<addr>` - Auth daemon address (default: `unix!/run/9auth/socket`)
- `--auth-id=<id>` - Server identity (enables authentication)
- `--aname=<path>` - Remote attach path (default: `/`)

## Examples

### Unauthenticated

```sh
9mount tcp!localhost!5640 /mnt/test
```

### Authenticated

Requires your public key imported on server (see [`9auth`](../9auth/README.md)):

```sh
9mount --auth-id=nas tcp!nas!5640 ~/media
9mount --auth-id=nas --aname=/public tcp!nas!5640 ~/public
```

### Production (NixOS)

```nix
services."9mount" = {
  enable = true;
  mounts = [{
    name = "media";
    dial = "tcp!nas!5640";
    mountPoint = "/mnt/media";
    authId = "nas";
  }];
};
```

Creates `9mount-media.service` that starts on boot.

## Unmounting

```sh
fusermount3 -u /mnt/media
# or
sudo umount /mnt/media
```

## Automatic Reconnection

Transparent recovery from network failures and server restarts:

1. I/O operation fails → returns `-EIO`
2. Next I/O triggers reconnect attempt
3. Exponential backoff with jitter: 0.5-1s, 1-2s, 2-4s, 4-8s, up to 30-60s
4. On reconnect, operations resume

**Preserved:** Open file handles, mount point accessibility
**Lost:** In-flight operations (fail with `-EIO`)

Jitter prevents thundering herd when server restarts with many clients.

## Authentication

When `--auth-id` is specified, mount connects to local 9auth daemon for challenge-response. Server verifies with your imported public key.

See [`9auth`](../9auth/README.md) for key setup.
