{
  perSystem = {
    lib,
    pkgs,
    ...
  }: let
    cmdPackage = import ../flake-parts/cmd-package.nix {inherit lib pkgs;};
  in {
    packages = cmdPackage.mkCmdPackage {
      pname = "media-player";
      description = "mpv-based media browser with 9P watch progress sync";
      version = "0.3.0";
      buildInputs = [
        pkgs.ncurses
      ];
      runtimeInputs = [
        pkgs.mpv
      ];
      extraLinkFlags = "-lncurses -lpthread -lm";
    };
  };
}
