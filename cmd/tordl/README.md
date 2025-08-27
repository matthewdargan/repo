# Tordl

Tordl downloads provided torrents.

Usage:

    tordl [-f file]

Tordl downloads torrents specified via magnet links. It expects input
of the form:

    Title<tab>magnet link

The -f flag reads magnet links from the specified file instead of
standard input.

## Examples

Download torrents from `torrents.txt`:

    tordl -f torrents.txt

Search for torrents and download search results:

    tor -f '1' -c '1_2' -u 'user 1' -s 'downloads' -o 'desc' -q 'The Office' | tordl
