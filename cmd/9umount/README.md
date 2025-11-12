# 9umount

Unmount 9P filesystems.

## Usage

```sh
9umount <mtpt>...
```

## Examples

Unmount a single mount point:

```sh
9umount ~/n/sources
```

Unmount multiple mount points:

```sh
9umount ~/n/sources ~/sources
```

Complete workflow:

```sh
# Mount
9mount 'tcp!sources.cs.bell-labs.com' ~/n/sources
9bind ~/n/sources ~/sources

# Unmount
9umount ~/n/sources ~/sources
```
