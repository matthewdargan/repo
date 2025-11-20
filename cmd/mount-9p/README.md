# mount.9p

mount(8) helper for 9P filesystems. Invoked by mount(8) when mounting filesystems with type 9p.

## Usage

mount.9p is called by mount(8), not directly:

```sh
mount -t 9p -o port=4500 nas /mnt/nas
```

## Options

Options passed via `-o` (comma-separated):

- `port=<port>` - TCP port (default: 564)
- `aname=<path>` - Remote path to attach (default: root)
- `msize=<bytes>` - Maximum 9P message size

## Examples

Mount a 9P server:

```sh
mount -t 9p -o port=4500 nas /mnt/nas
```

Use with systemd.mounts:

```nix
systemd.mounts = [{
  what = "nas";
  where = "/home/user/nas";
  type = "9p";
  options = "port=4500";
}];
```
