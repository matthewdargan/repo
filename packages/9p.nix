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
             -DBUILD_DEBUG=0 cmd/9p/main.c -o 9p-bin
        '';
        installPhase = ''
          mkdir -p "$out/bin"
          cp 9p-bin "$out/bin/9p"
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
             -DBUILD_DEBUG=1 cmd/9p/main.c -o 9p-debug
        '';
        dontStrip = true;
        installPhase = ''
          mkdir -p "$out/bin"
          cp 9p-debug "$out/bin/9p"
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
