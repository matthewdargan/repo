{
  perSystem = {
    lib,
    pkgs,
    ...
  }: {
    packages = {
      "9bind" = pkgs.clangStdenv.mkDerivation {
        buildPhase = ''
          clang -O3 -I. -g -fdiagnostics-absolute-paths -Wall -Wextra \
             cmd/9bind/main.c -o 9bind
        '';
        installPhase = ''
          mkdir -p "$out/bin"
          cp 9bind "$out/bin"
        '';
        meta = with lib; {
          description = "Performs a bind mount";
          homepage = "https://github.com/matthewdargan/repo";
          license = licenses.bsd3;
          maintainers = with maintainers; [matthewdargan];
        };
        pname = "9bind";
        src = ../.;
        version = "0.1.0";
      };
      "9bind-debug" = pkgs.clangStdenv.mkDerivation {
        buildPhase = ''
          clang -O0 -g -fsanitize=address -fno-omit-frame-pointer \
             -I. -fdiagnostics-absolute-paths -Wall -Wextra \
             cmd/9bind/main.c -o 9bind
        '';
        dontStrip = true;
        installPhase = ''
          mkdir -p "$out/bin"
          cp 9bind "$out/bin"
        '';
        meta = with lib; {
          description = "Performs a bind mount";
          homepage = "https://github.com/matthewdargan/repo";
          license = licenses.bsd3;
          maintainers = with maintainers; [matthewdargan];
        };
        pname = "9bind";
        src = ../.;
        version = "0.1.0";
      };
    };
  };
}
