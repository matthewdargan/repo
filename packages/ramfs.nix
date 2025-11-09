{
  perSystem = {
    lib,
    pkgs,
    ...
  }: let
    cmdPackage = import ../flake-parts/cmd-package.nix {inherit lib pkgs;};
  in {
    packages = cmdPackage.mkCmdPackage {
      pname = "ramfs";
      description = "In-memory filesystem using 9P protocol";
      version = "0.1.0";
    };
  };
}
