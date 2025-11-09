# 9p

A command-line tool for interacting with 9P file servers. Provides basic file operations over the [9P protocol](https://9fans.github.io/plan9port/man/man9/intro.html).

## Usage

```sh
9p [-a=address] [-A=aname] cmd args...
```

**Options:**

- `-a=address` - 9P server address (e.g., `tcp!server!564`)
- `-A=aname` - Attach name for servers exporting multiple file trees

**Commands:**

- `create name...` - Create files
- `read name` - Read file contents to stdout
- `write name` - Write stdin to file
- `remove name...` - Remove files
- `stat name` - Display file metadata
- `ls name...` - List directory contents

## Examples

List files on a remote 9P server:

```sh
9p -a='tcp!sources.cs.bell-labs.com' ls /
```

Read a file from a remote server:

```sh
9p -a='tcp!server!564' read /path/to/file
```

Write to a file on a remote server:

```sh
echo "hello world" | 9p -a='tcp!server!564' write /path/to/file
```

Get file metadata:

```sh
9p -a='tcp!server!564' stat /path/to/file
```

Create a file:

```sh
9p -a='tcp!server!564' create /path/to/newfile
```

Remove files:

```sh
9p -a='tcp!server!564' remove /path/to/file1 /path/to/file2
```

Use with a specific attach name:

```sh
9p -a='tcp!server!564' -A='/home/user' ls /
```
