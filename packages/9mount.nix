{
  perSystem = {
    lib,
    pkgs,
    ...
  }: let
    cmdPackage = import ../flake-parts/cmd-package.nix {inherit lib pkgs;};
  in {
    packages = cmdPackage.mkCmdPackage {
      pname = "9mount";
      description = "Mounts a 9p filesystem";
      version = "0.1.0";
      buildInputs = [pkgs.fuse3];
      extraLinkFlags = "-lfuse3 -lpthread";
    };
  };
}
