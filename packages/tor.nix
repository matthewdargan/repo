{
  perSystem = {
    lib,
    pkgs,
    ...
  }: {
    packages = {
      tor = pkgs.clangStdenv.mkDerivation {
        buildInputs = [
          pkgs.curl
          pkgs.libxml2
          pkgs.pkg-config
        ];
        buildPhase = ''
          clang -O3 -I. -fdiagnostics-absolute-paths -Wall -Wextra \
             $(pkg-config --cflags --libs libxml-2.0) -lcurl \
             cmd/tor/main.c -o tor
        '';
        installPhase = ''
          mkdir -p "$out/bin"
          cp tor "$out/bin"
        '';
        meta = with lib; {
          description = "Queries for torrents";
          homepage = "https://github.com/matthewdargan/repo";
          license = licenses.bsd3;
          maintainers = with maintainers; [matthewdargan];
        };
        nativeBuildInputs = [pkgs.pkg-config];
        pname = "tor";
        src = ../.;
        version = "0.1.1";
      };
      tor-debug = pkgs.clangStdenv.mkDerivation {
        buildInputs = [
          pkgs.curl
          pkgs.libxml2
          pkgs.pkg-config
        ];
        buildPhase = ''
          clang -O0 -g -fsanitize=address -fno-omit-frame-pointer \
             -I. -fdiagnostics-absolute-paths -Wall -Wextra \
             $(pkg-config --cflags --libs libxml-2.0) -lcurl \
             cmd/tor/main.c -o tor
        '';
        installPhase = ''
          mkdir -p "$out/bin"
          cp tor "$out/bin"
        '';
        meta = with lib; {
          description = "Queries for torrents";
          homepage = "https://github.com/matthewdargan/repo";
          license = licenses.bsd3;
          maintainers = with maintainers; [matthewdargan];
        };
        nativeBuildInputs = [pkgs.pkg-config];
        pname = "tor";
        src = ../.;
        version = "0.1.1";
      };
    };
  };
}
