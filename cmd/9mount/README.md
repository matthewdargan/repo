# 9mount

Mount a [9P filesystem](https://9fans.github.io/plan9port/man/man9/intro.html) at a mount point.

## Usage

```sh
9mount [-nsx] [-a=<spec>] [-m=<msize>] [-u=<uid>] [-g=<gid>] <dial> <mtpt>
```

**Dial formats:**

- `unix!SOCKET` - Unix domain socket
- `tcp!HOST[!PORT]` - TCP connection
- `virtio!CHANNEL` - Virtio-9p channel
- `-` - stdin/stdout

**Options:**

- `-n` - Print mount command to stderr without mounting
- `-s` - Single attach mode (all users see same filesystem)
- `-x` - Exclusive access (no other users can access)
- `-a=spec` - File tree to attach (for servers exporting multiple trees)
- `-m=msize` - Maximum 9P message length in bytes
- `-u=uid` - UID for mounting
- `-g=gid` - GID for mounting

## Examples

Mount Plan 9 sources archive:

```sh
9mount 'tcp!sources.cs.bell-labs.com' ~/n/sources
```

Mount via unix socket:

```sh
9mount 'unix!/tmp/ns.'$USER'.:0/factotum' ~/n/factotum
```

Mount with specific attach name:

```sh
9mount -a='/home/user/mail' 'tcp!server!5640' ~/mail
```

Mount via virtio-9p:

```sh
9mount 'virtio!share' ~/n/host
```

Mount via stdin/stdout:

```sh
u9fs | 9mount - ~/n/fs
```
