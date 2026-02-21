{
  perSystem = {
    lib,
    pkgs,
    ...
  }: let
    cmdPackage = import ../flake-parts/cmd-package.nix {inherit lib pkgs;};

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
      extraLinkFlags = "-lavformat -lavcodec -lavutil -lswresample -lm";

      postPatch = ''
        DASHJS_CONTENT=$(cat ${dashjs} | \
          sed 's/\\/\\\\/g' | \
          sed 's/"/\\"/g' | \
          sed 's/%/%%/g' | \
          sed ':a;N;$!ba;s/\n/\\n/g')

        substituteInPlace cmd/media-server/main.c \
          --replace-fail '@@DASHJS@@' "$DASHJS_CONTENT"

        echo '// AUTO-GENERATED from player.js' > cmd/media-server/player.js.inc
        echo 'internal String8 player_js_template = str8_lit_comp(' >> cmd/media-server/player.js.inc
        cat cmd/media-server/player.js | while IFS= read -r line; do
          line=$(echo "$line" | sed 's/\\/\\\\/g' | sed 's/"/\\"/g')
          echo "\"$line\\n\"" >> cmd/media-server/player.js.inc
        done
        echo ');' >> cmd/media-server/player.js.inc
      '';
    };
  };
}
