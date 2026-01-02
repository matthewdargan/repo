# 9pfs

[9P](https://9fans.github.io/plan9port/man/man9/intro.html) file server that exports local directories over the network.

## Usage

```sh
9pfs [options] <address>
```

**Options:**

- `--root=<path>` - Root directory to serve (default: current directory)
- `--readonly` - Serve in read-only mode
- `--threads=<n>` - Number of worker threads (default: max(4, cores/4))

**Address formats:**

- `tcp!HOST!PORT` - TCP connection (hostname or IP)
- `unix!SOCKET` - Unix domain socket

## Examples

Serve current directory on TCP port 5640:

```sh
9pfs 'tcp!localhost!5640'
```

Serve a specific directory:

```sh
9pfs --root=/home/user/share 'tcp!0.0.0.0!5640'
```

Serve in read-only mode:

```sh
9pfs --readonly --root=/var/www 'tcp!0.0.0.0!5640'
```

Serve with custom thread count:

```sh
9pfs --threads=8 'tcp!localhost!5640'
```

Serve via Unix socket:

```sh
9pfs 'unix!/tmp/9pfs.sock'
```
