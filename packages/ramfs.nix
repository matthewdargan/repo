{
  perSystem = {
    lib,
    pkgs,
    ...
  }: {
    packages = {
      "ramfs" = pkgs.clangStdenv.mkDerivation {
        buildPhase = ''
          clang -O3 -I. -g -fdiagnostics-absolute-paths -Wall -Wextra \
             cmd/ramfs/main.c -o ramfs
        '';
        installPhase = ''
          mkdir -p "$out/bin"
          cp ramfs "$out/bin"
        '';
        meta = with lib; {
          description = "In-memory filesystem using 9P protocol";
          homepage = "https://github.com/matthewdargan/repo";
          license = licenses.bsd3;
          maintainers = with maintainers; [matthewdargan];
        };
        pname = "ramfs";
        src = ../.;
        version = "0.1.0";
      };
      "ramfs-debug" = pkgs.clangStdenv.mkDerivation {
        buildPhase = ''
          clang -O0 -g -fsanitize=address -fno-omit-frame-pointer \
             -I. -fdiagnostics-absolute-paths -Wall -Wextra \
             cmd/ramfs/main.c -o ramfs
        '';
        dontStrip = true;
        installPhase = ''
          mkdir -p "$out/bin"
          cp ramfs "$out/bin"
        '';
        meta = with lib; {
          description = "In-memory filesystem using 9P protocol";
          homepage = "https://github.com/matthewdargan/repo";
          license = licenses.bsd3;
          maintainers = with maintainers; [matthewdargan];
        };
        pname = "ramfs";
        src = ../.;
        version = "0.1.0";
      };
    };
  };
}
