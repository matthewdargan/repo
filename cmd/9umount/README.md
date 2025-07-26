# 9umount

9umount unmounts 9p filesystems.

Usage:

    9umount mtpt...

## Example

Mount 9p filesystems:

    9mount 'tcp!sources.cs.bell-labs.com' $HOME/n/sources
    9bind $HOME/n/sources $HOME/sources

Unmount 9p filesystems:

    9umount $HOME/n/sources $HOME/sources
