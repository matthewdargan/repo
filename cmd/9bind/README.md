# 9bind

9bind performs a bind mount, making the file tree visible at directory
old also visible at mount point new.

Usage:

    9bind old new

## Example

Mount a 9p filesystem:

    9mount 'tcp!sources.cs.bell-labs.com' $HOME/n/sources

Bind to make the same filesystem visible at another location:

    9bind $HOME/n/sources $HOME/sources
