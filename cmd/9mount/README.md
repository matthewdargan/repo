# 9mount

Mount a [9P filesystem](https://9fans.github.io/plan9port/man/man9/intro.html) at a mount point.

## Usage

```sh
9mount [options] <dial> <mtpt>
```

**Options:**

- `--dry-run` - Print mount command without executing
- `--aname=<path>` - Remote path to attach (default: root)
- `--msize=<bytes>` - Maximum 9P message size
- `--uid=<uid>` - User ID for mount (default: current user)
- `--gid=<gid>` - Group ID for mount (default: current group)

**Dial formats:**

- `tcp!HOST!PORT` - TCP connection (hostname or IP)
- `unix!SOCKET` - Unix domain socket
- `-` - stdin/stdout

## Examples

Mount a 9P server over TCP:

```sh
9mount 'tcp!nas!564' ~/n/nas
```

Mount via unix socket:

```sh
9mount 'unix!/tmp/9p.sock' ~/n/remote
```

Mount with specific attach name:

```sh
9mount --aname='/home/user/mail' 'tcp!server!5640' ~/mail
```

Dry run to see mount command:

```sh
9mount --dry-run 'tcp!localhost!5640' ~/n/test
```

Mount via stdin/stdout:

```sh
u9fs | 9mount - ~/n/fs
```
