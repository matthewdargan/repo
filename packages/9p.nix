{
  perSystem = {
    lib,
    pkgs,
    ...
  }: {
    packages = {
      "9p" = pkgs.clangStdenv.mkDerivation {
        buildPhase = ''
          clang -O3 -I. -g -fdiagnostics-absolute-paths -Wall -Wextra \
             cmd/9p/main.c -o 9p
        '';
        installPhase = ''
          mkdir -p "$out/bin"
          cp 9p "$out/bin"
        '';
        meta = with lib; {
          description = "Read and write files on a 9p server";
          homepage = "https://github.com/matthewdargan/repo";
          license = licenses.bsd3;
          maintainers = with maintainers; [matthewdargan];
        };
        pname = "9p";
        src = ../.;
        version = "0.1.0";
      };
      "9p-debug" = pkgs.clangStdenv.mkDerivation {
        buildPhase = ''
          clang -O0 -g -fsanitize=address -fno-omit-frame-pointer \
             -I. -fdiagnostics-absolute-paths -Wall -Wextra \
             -DBUILD_DEBUG cmd/9p/main.c -o 9p
        '';
        dontStrip = true;
        installPhase = ''
          mkdir -p "$out/bin"
          cp 9p "$out/bin"
        '';
        meta = with lib; {
          description = "Read and write files on a 9p server";
          homepage = "https://github.com/matthewdargan/repo";
          license = licenses.bsd3;
          maintainers = with maintainers; [matthewdargan];
        };
        pname = "9p";
        src = ../.;
        version = "0.1.0";
      };
    };
  };
}
