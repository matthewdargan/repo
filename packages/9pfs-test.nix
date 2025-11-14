{
  perSystem = {
    lib,
    pkgs,
    ...
  }: let
    cmdPackage = import ../flake-parts/cmd-package.nix {inherit lib pkgs;};
  in {
    packages = cmdPackage.mkCmdPackage {
      pname = "9pfs-test";
      description = "Test program for 9pfs";
      version = "0.1.0";
    };
  };
}
