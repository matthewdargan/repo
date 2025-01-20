# Mooch

Mooch queries and downloads torrents.

Usage:

    mooch [-f filter] [-c category] [-u user] [-s sort] [-o order] <-q query>

Mooch queries torrents matching the specified criteria and adds them to a
[libtorrent](https://www.libtorrent.org/index.html) session.

The -f flag sets the filter. Available options include: 0 (No filter),
1 (No remakes), 2 (Trusted only). The default setting is 0.

The -c flag sets the category. The default setting is
0_0 (All categories).

The -u flag sets the user. The default setting does not set a specific
user.

The -s flag sets the sort method. Available sort methods include:
comments, size, id (date), seeders, leechers, downloads. The default
setting sorts by date.

The -o flag sets the sorting order. The order can be set to
ascending (asc) or descending (desc). The default setting does not set
an order.

The -q flag sets the query. This is a required argument.

Example:

    mooch -f='1' -c='1_2' -u='user 1' -s='downloads' -o='desc' -q='One Piece'
