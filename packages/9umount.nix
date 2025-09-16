{
  perSystem = {
    lib,
    pkgs,
    ...
  }: {
    packages = {
      "9umount" = pkgs.clangStdenv.mkDerivation {
        buildPhase = ''
          clang -O3 -I. -g -fdiagnostics-absolute-paths -Wall -Wextra \
             cmd/9umount/main.c -o 9umount
        '';
        installPhase = ''
          mkdir -p "$out/bin"
          cp 9umount "$out/bin"
        '';
        meta = with lib; {
          description = "Unmounts a 9p filesystem";
          homepage = "https://github.com/matthewdargan/repo";
          license = licenses.bsd3;
          maintainers = with maintainers; [matthewdargan];
        };
        pname = "9umount";
        src = ../.;
        version = "0.1.0";
      };
      "9umount-debug" = pkgs.clangStdenv.mkDerivation {
        buildPhase = ''
          clang -O0 -g -fsanitize=address -fno-omit-frame-pointer \
             -I. -fdiagnostics-absolute-paths -Wall -Wextra \
             cmd/9umount/main.c -o 9umount
        '';
        dontStrip = true;
        installPhase = ''
          mkdir -p "$out/bin"
          cp 9umount "$out/bin"
        '';
        meta = with lib; {
          description = "Unmounts a 9p filesystem";
          homepage = "https://github.com/matthewdargan/repo";
          license = licenses.bsd3;
          maintainers = with maintainers; [matthewdargan];
        };
        pname = "9umount";
        src = ../.;
        version = "0.1.0";
      };
    };
  };
}
