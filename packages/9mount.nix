{
  perSystem = {
    lib,
    pkgs,
    ...
  }: {
    packages = {
      "9mount" = pkgs.clangStdenv.mkDerivation {
        buildPhase = ''
          clang -O3 -I. -g -fdiagnostics-absolute-paths -Wall -Wextra \
             cmd/9mount/main.c -o 9mount
        '';
        installPhase = ''
          mkdir -p "$out/bin"
          cp 9mount "$out/bin"
        '';
        meta = with lib; {
          description = "Mounts a 9p filesystem";
          homepage = "https://github.com/matthewdargan/repo";
          license = licenses.bsd3;
          maintainers = with maintainers; [matthewdargan];
        };
        pname = "9mount";
        src = ../.;
        version = "0.1.0";
      };
      "9mount-debug" = pkgs.clangStdenv.mkDerivation {
        buildPhase = ''
          clang -O0 -g -fsanitize=address -fno-omit-frame-pointer \
             -I. -fdiagnostics-absolute-paths -Wall -Wextra \
             cmd/9mount/main.c -o 9mount
        '';
        installPhase = ''
          mkdir -p "$out/bin"
          cp 9mount "$out/bin"
        '';
        meta = with lib; {
          description = "Mounts a 9p filesystem";
          homepage = "https://github.com/matthewdargan/repo";
          license = licenses.bsd3;
          maintainers = with maintainers; [matthewdargan];
        };
        pname = "9mount";
        src = ../.;
        version = "0.1.0";
      };
    };
  };
}
