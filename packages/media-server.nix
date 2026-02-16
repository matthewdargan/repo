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
      description = "DASH media server with pure libav integration";
      version = "0.2.0";
      buildInputs = [pkgs.ffmpeg.dev];
      extraLinkFlags = "-lavformat -lavcodec -lavutil";
    };
  };
}
