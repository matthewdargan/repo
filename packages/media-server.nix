{
  perSystem = {
    lib,
    pkgs,
    ...
  }: let
    cmdPackage = import ../flake-parts/cmd-package.nix {inherit lib pkgs;};

    # Vendor dash.js - compression-oriented, no CDN dependency
    dashjs = pkgs.fetchurl {
      url = "https://cdn.dashjs.org/v4.7.4/dash.all.min.js";
      hash = "sha256-Oh21HtAEEsFvntswTbaayE8f/OiqLWHK9Ilaqcm98N8=";
    };
  in {
    packages = cmdPackage.mkCmdPackage {
      pname = "media-server";
      description = "DASH media server with pure libav integration";
      version = "0.2.0";
      buildInputs = [pkgs.ffmpeg.dev];
      extraLinkFlags = "-lavformat -lavcodec -lavutil";

      # Inline dash.js at build time
      postPatch = ''
        # Escape for C string literal and str8f format string:
        # - Backslashes: \ -> \\
        # - Double quotes: " -> \"
        # - Percent signs: % -> %% (for str8f format string)
        # - Newlines: actual newline -> \n
        DASHJS_CONTENT=$(cat ${dashjs} | \
          sed 's/\\/\\\\/g' | \
          sed 's/"/\\"/g' | \
          sed 's/%/%%/g' | \
          sed ':a;N;$!ba;s/\n/\\n/g')

        # Substitute the placeholder with actual dash.js content
        substituteInPlace cmd/media-server/main.c \
          --replace-fail '@@DASHJS@@' "$DASHJS_CONTENT"
      '';
    };
  };
}
