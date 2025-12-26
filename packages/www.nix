{
  perSystem = {pkgs, ...}: {
    packages.www = pkgs.stdenv.mkDerivation {
      pname = "www";
      version = "0.1.0";
      src = pkgs.lib.cleanSource ./..;
      nativeBuildInputs = [pkgs.pandoc];
      buildPhase = ''
        # Convert markdown docs to HTML
        mkdir -p docs
        for md in docs/*.md; do
          if [ -f "$md" ]; then
            base=$(basename "$md" .md)
            ${pkgs.pandoc}/bin/pandoc \
              --standalone \
              --from markdown \
              --to html5 \
              --metadata title="$base" \
              --css="/style.css" \
              "$md" -o "docs/$base.html"
          fi
        done
      '';
      installPhase = ''
        mkdir -p $out/docs
        cp www/index.html $out/
        cp docs/*.html $out/docs/ 2>/dev/null || true
      '';
    };
  };
}
