# 9mount

9mount mounts a 9p filesystem served at dial on mtpt. Mtpt must be
writable by you and not sticky. Dial is a dial string assuming one of
the forms:

    unix!SOCKET
    tcp!HOST[!PORT]
    virtio!CHANNEL
    -

Usage:

    9mount [-nsx] [-a spec] [-m msize] [-u uid] [-g gid] dial mtpt

The -n flag prints the mount command to stderr instead of performing
the mount.

The -s flag enables single attach mode so all users accessing the mount
point see the same filesystem.

The -x flag enables exclusive access so other users cannot access the
mount point.

The -a flag sets the file tree to mount when attaching to servers that
export multiple file trees.

The -m flag sets the maximum length of a single 9p message in bytes.

The -u flag sets the UID for mounting the filesystem.

The -g flag sets the GID for mounting the filesystem.

## Examples

Mount plan9port's factotum interface via unix socket:

    9mount 'unix!/tmp/ns.'$USER'.:0/factotum' $HOME/n/factotum

Import plan 9 "sources" archive:

    9mount 'tcp!sources.cs.bell-labs.com' $HOME/n/sources

Import maildir from remote server:

    9mount -a /home/user/mail 'tcp!server!5640' $HOME/mail

Mount host filesystem shared via virtio-9p:

    9mount 'virtio!share' $HOME/n/host

Mount a 9p server via stdin/stdout:

    u9fs | 9mount - $HOME/n/fs
