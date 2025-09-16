{
  perSystem = {
    lib,
    pkgs,
    ...
  }: {
    packages = {
      tordl = pkgs.clangStdenv.mkDerivation {
        buildInputs = [
          pkgs.boost
          pkgs.libtorrent-rasterbar
          pkgs.pkg-config
        ];
        buildPhase = ''
          clang++ -O3 -I. -g -fdiagnostics-absolute-paths -Wall -Wextra \
             $(pkg-config --cflags --libs libtorrent-rasterbar) \
             cmd/tordl/main.cpp -o tordl
        '';
        installPhase = ''
          mkdir -p "$out/bin"
          cp tordl "$out/bin"
        '';
        meta = with lib; {
          description = "Downloads provided torrents";
          homepage = "https://github.com/matthewdargan/repo";
          license = licenses.bsd3;
          maintainers = with maintainers; [matthewdargan];
        };
        nativeBuildInputs = [pkgs.pkg-config];
        pname = "tordl";
        src = ../.;
        version = "0.1.1";
      };
      tordl-debug = pkgs.clangStdenv.mkDerivation {
        buildInputs = [
          pkgs.boost
          pkgs.libtorrent-rasterbar
          pkgs.pkg-config
        ];
        buildPhase = ''
          clang++ -O0 -g -fsanitize=address -fno-omit-frame-pointer \
             -I. -fdiagnostics-absolute-paths -Wall -Wextra \
             $(pkg-config --cflags --libs libtorrent-rasterbar) \
             cmd/tordl/main.cpp -o tordl
        '';
        dontStrip = true;
        installPhase = ''
          mkdir -p "$out/bin"
          cp tordl "$out/bin"
        '';
        meta = with lib; {
          description = "Downloads provided torrents";
          homepage = "https://github.com/matthewdargan/repo";
          license = licenses.bsd3;
          maintainers = with maintainers; [matthewdargan];
        };
        nativeBuildInputs = [pkgs.pkg-config];
        pname = "tordl";
        src = ../.;
        version = "0.1.1";
      };
    };
  };
}
