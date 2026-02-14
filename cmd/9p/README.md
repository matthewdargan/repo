# 9p

CLI tool for one-shot 9P operations. Lightweight 9P client for single operations (ls, read, write, stat, create, remove). Opens connection, performs operation, exits. For persistent access, use [`9mount`](../9mount/README.md).

## Usage

```sh
9p [options] <address> <cmd> <args>
```

**Arguments:**
- `<address>` - Server address (`tcp!nas!5640`, `unix!/tmp/9pfs.sock`)
- `<cmd>` - Command (ls, stat, read, write, create, remove)
- `<args>` - Command-specific arguments

**Options:**
- `--auth-daemon=<addr>` - Auth daemon address (default: `unix!/run/9auth/socket`)
- `--auth-id=<id>` - Server identity (enables authentication)
- `--aname=<path>` - Remote attach path (default: `/`)

## Commands

- `ls <name>...` - List directory contents
- `stat <name>` - Display file metadata
- `read <name>` - Read file to stdout
- `write <name>` - Write stdin to file
- `create <name>...` - Create files
- `remove <name>...` - Remove files

## Examples

### Unauthenticated

```sh
9p tcp!localhost!5640 ls /
9p tcp!localhost!5640 read /README.md
echo "test" | 9p tcp!localhost!5640 write /test.txt
9p tcp!localhost!5640 stat /data/file.txt
9p tcp!localhost!5640 create /new1.txt /new2.txt
9p tcp!localhost!5640 remove /old.txt
```

### Authenticated

Requires your public key imported on server (see [`9auth`](../9auth/README.md)):

```sh
9p --auth-id=nas tcp!nas!5640 ls /
9p --auth-id=nas tcp!nas!5640 read /secret.txt
echo "data" | 9p --auth-id=nas tcp!nas!5640 write /file.txt
```

### Unix Socket

```sh
9p unix!/tmp/9pfs.sock ls /
9p unix!/tmp/9pfs.sock stat /file.txt
```

### Attach Path

```sh
# "/" refers to server's "/public"
9p --aname=/public tcp!nas!5640 ls /
9p --aname=/home/user tcp!nas!5640 read /docs/report.txt
```

## Command Details

### ls - List Directory

```sh
9p tcp!localhost!5640 ls /
9p tcp!localhost!5640 ls /home /var /tmp
```

### stat - File Metadata

```sh
9p tcp!localhost!5640 stat /data/file.txt
```

### read - Read File

```sh
9p tcp!localhost!5640 read /config.json
9p tcp!localhost!5640 read /remote.dat > local.dat
9p tcp!localhost!5640 read /data.json | jq .
```

### write - Write File

```sh
echo "hello" | 9p tcp!localhost!5640 write /test.txt
cat local.txt | 9p tcp!localhost!5640 write /remote.txt
curl https://example.com/data | 9p tcp!localhost!5640 write /cache.html
```

### create - Create Files

```sh
9p tcp!localhost!5640 create /newfile.txt
9p tcp!localhost!5640 create /a.txt /b.txt /c.txt
```

### remove - Remove Files

```sh
9p tcp!localhost!5640 remove /oldfile.txt
9p tcp!localhost!5640 remove /tmp/a /tmp/b /tmp/c
```

## Scripting Examples

**Backup files:**
```sh
#!/bin/bash
for file in config.json data.db logs.txt; do
  9p --auth-id=nas tcp!nas!5640 read "/$file" > "backup/$file"
done
```

**Upload artifacts:**
```sh
#!/bin/bash
tar -czf - dist/ | 9p --auth-id=build tcp!build!5640 write /artifacts/$(date +%Y%m%d).tar.gz
```

**Health check:**
```sh
#!/bin/bash
if 9p tcp!localhost!5640 stat /health >/dev/null 2>&1; then
  echo "Server healthy"
else
  echo "Server down"
  exit 1
fi
```
