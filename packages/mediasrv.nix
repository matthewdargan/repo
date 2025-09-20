{
  perSystem = {
    lib,
    pkgs,
    ...
  }: {
    packages = {
      mediasrv = pkgs.clangStdenv.mkDerivation {
        buildInputs = [pkgs.ffmpeg];
        buildPhase = ''
          clang -O3 -I. -g -fdiagnostics-absolute-paths -Wall -Wextra -lavcodec -lavformat -lavutil \
             cmd/mediasrv/main.c -o mediasrv
        '';
        installPhase = ''
          mkdir -p "$out/bin"
          cp mediasrv "$out/bin"
        '';
        meta = with lib; {
          description = "Media server";
          homepage = "https://github.com/matthewdargan/repo";
          license = licenses.bsd3;
          maintainers = with maintainers; [matthewdargan];
        };
        pname = "mediasrv";
        src = ../.;
        version = "0.10.0";
      };
      mediasrv-debug = pkgs.clangStdenv.mkDerivation {
        buildInputs = [pkgs.ffmpeg];
        buildPhase = ''
          clang -O0 -g -fsanitize=address -fno-omit-frame-pointer -I. -Wall -Wextra -lavcodec -lavformat -lavutil \
             cmd/mediasrv/main.c -o mediasrv
        '';
        dontStrip = true;
        installPhase = ''
          mkdir -p "$out/bin"
          cp mediasrv "$out/bin"
        '';
        meta = with lib; {
          description = "Media server";
          homepage = "https://github.com/matthewdargan/repo";
          license = licenses.bsd3;
          maintainers = with maintainers; [matthewdargan];
        };
        pname = "mediasrv";
        src = ../.;
        version = "0.10.0";
      };
    };
  };
}
