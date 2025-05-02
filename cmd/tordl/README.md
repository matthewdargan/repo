# Tordl

Tordl downloads provided torrents.

Usage:

    tordl [-f file]

Tordl downloads torrents specified via magnet links. It expects input
of the form:

    Title<tab>magnet link

The -f flag reads magnet links from the specified file instead of
standard input

Example:

    tordl -f torrents.txt

    tor -f '1' -c '1_2' -u 'user 1' -s 'downloads' -o 'desc' -q 'One Piece' | tordl
