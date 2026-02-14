# 9pfs

9P file server that exports local directories over the network. Serves directory trees via 9P protocol. Clients connect over TCP or unix sockets. Supports disk-backed storage, in-memory tmp, read-only mode, and authentication via [`9auth`](../9auth/README.md).

## Usage

```sh
9pfs [options] <address>
```

**Arguments:**
- `<address>` - Dial string (`tcp!*!5640`, `unix!/tmp/9pfs.sock`)

**Options:**
- `--root=<path>` - Root directory to serve (default: current directory)
- `--readonly` - Read-only mode (reject writes/creates/deletes)
- `--threads=<n>` - Worker threads (default: max(4, cores/4))
- `--auth-daemon=<addr>` - Auth daemon address (default: `unix!/run/9auth/socket`)
- `--auth-id=<id>` - Server identity (enables authentication)

## Examples

### Unauthenticated

```sh
9pfs tcp!localhost!5640
9pfs --root=/var/www tcp!*!8080
9pfs --readonly --root=/usr/share/doc tcp!*!5640
```

### Authenticated

Import client public keys first (see [`9auth`](../9auth/README.md)):

```sh
# Import client keys
9auth --import < alice-nas.pubkey
9auth --import < bob-nas.pubkey

# Start server
9pfs --auth-id=nas --root=/media tcp!*!5640
```

Only clients with imported public keys can connect.

### Unix Socket

```sh
9pfs --root=/tmp/test unix!/tmp/9pfs.sock
```

### Production (NixOS)

```nix
systemd.services.media-serve = {
  description = "9P Media Server";
  after = ["9auth.service" "network.target"];
  requires = ["9auth.service"];
  wantedBy = ["multi-user.target"];

  serviceConfig = {
    ExecStart = "${pkgs.9pfs}/bin/9pfs --auth-id=nas --root=/media tcp!*!5640";
    Restart = "always";
  };
};
```

## Dial String Format

Format: `protocol!host!port`

**TCP:**
- `tcp!*!5640` - All interfaces
- `tcp!localhost!5640` - Localhost only
- `tcp!192.168.1.10!5640` - Specific IP

**Unix:**
- `unix!/tmp/9pfs.sock`
- `unix!/run/9pfs/socket`

## Read-Only Mode

```sh
9pfs --readonly --root=/usr/share/books tcp!*!5640
```

**Rejected:** Twrite, Tcreate, Tremove, Twstat

**Allowed:** Tread, Tstat, Twalk

## Security

**Without `--auth-id`:** Anyone with network access can read/write

**With `--auth-id`:** Only clients with imported public keys authenticate

Best practices:
- Use `--auth-id` for network-exposed servers
- Use `--readonly` for public content
- Run as unprivileged user
- Bind to specific IP, not `*`, unless necessary
