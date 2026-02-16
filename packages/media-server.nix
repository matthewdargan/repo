{
  perSystem = {
    lib,
    pkgs,
    ...
  }: let
    cmdPackage = import ../flake-parts/cmd-package.nix {inherit lib pkgs;};
  in {
    packages = cmdPackage.mkCmdPackage {
      pname = "media-server";
      description = "DASH media server using FFmpeg subprocess";
      version = "0.2.0";
      buildInputs = [];
      nativeBuildInputs = [pkgs.makeWrapper];
      postInstall = ''
        wrapProgram $out/bin/media-server \
          --prefix PATH : ${lib.makeBinPath [pkgs.ffmpeg]}
      '';
    };
  };
}
