# 9bind

Bind mount a directory, making the file tree at `old` also visible at `new`.

## Usage

```sh
9bind old new
```

## Examples

Mount a 9P filesystem and bind it to another location:

```sh
9mount 'tcp!sources.cs.bell-labs.com' ~/n/sources
9bind ~/n/sources ~/sources
```

Make a directory accessible at multiple locations:

```sh
9bind /usr/share ~/share
```
