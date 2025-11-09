{
  perSystem = {
    lib,
    pkgs,
    ...
  }: let
    cmdPackage = import ../flake-parts/cmd-package.nix {inherit lib pkgs;};
  in {
    packages = cmdPackage.mkCmdPackage {
      pname = "9umount";
      description = "Unmounts a 9p filesystem";
      version = "0.1.0";
    };
  };
}
