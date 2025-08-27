{
  perSystem = {
    lib,
    pkgs,
    ...
  }: {
    packages = {
      torrss = pkgs.clangStdenv.mkDerivation {
        buildInputs = [
          pkgs.boost
          pkgs.curl
          pkgs.libtorrent-rasterbar
          pkgs.libxml2
          pkgs.pkg-config
        ];
        buildPhase = ''
          clang++ -O3 -I. -g -fdiagnostics-absolute-paths -Wall -Wextra \
             $(pkg-config --cflags --libs libxml-2.0 libtorrent-rasterbar) -lcurl \
             cmd/torrss/main.cpp -o torrss
        '';
        installPhase = ''
          mkdir -p "$out/bin"
          cp torrss "$out/bin"
        '';
        meta = with lib; {
          description = "Queries RSS feeds and downloads torrents automatically";
          homepage = "https://github.com/matthewdargan/repo";
          license = licenses.bsd3;
          maintainers = with maintainers; [matthewdargan];
        };
        nativeBuildInputs = [pkgs.pkg-config];
        pname = "torrss";
        src = ../.;
        version = "0.1.1";
      };
      torrss-debug = pkgs.clangStdenv.mkDerivation {
        buildInputs = [
          pkgs.boost
          pkgs.curl
          pkgs.libtorrent-rasterbar
          pkgs.libxml2
          pkgs.pkg-config
        ];
        buildPhase = ''
          clang++ -O0 -g -fsanitize=address -fno-omit-frame-pointer \
             -I. -fdiagnostics-absolute-paths -Wall -Wextra \
             $(pkg-config --cflags --libs libxml-2.0 libtorrent-rasterbar) -lcurl \
             cmd/torrss/main.cpp -o torrss
        '';
        installPhase = ''
          mkdir -p "$out/bin"
          cp torrss "$out/bin"
        '';
        meta = with lib; {
          description = "Queries RSS feeds and downloads torrents automatically";
          homepage = "https://github.com/matthewdargan/repo";
          license = licenses.bsd3;
          maintainers = with maintainers; [matthewdargan];
        };
        nativeBuildInputs = [pkgs.pkg-config];
        pname = "torrss";
        src = ../.;
        version = "0.1.1";
      };
    };
  };
}
