# Mediasrv

Mediasrv streams media content in DASH format and serves static web
content.

Usage:

    mediasrv [-i inputmtpt] [-o outputmtpt] [-p port]

The -i flag sets the input mount point where media files are located. If
not provided, files are served related to the current working directory.

The -o flag sets the output mount point where generated DASH manifests,
media segments, and subtitles will be stored.

The -p flag sets the server port. The default is 8080.

## Examples

Run the server:

    mediasrv -i /media/shows -o /var/lib/mediasrv -p 8081

Play a video from the server:

    play 'shows/the-office/season01/e01.mkv'
