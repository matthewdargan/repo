{
  perSystem = {
    lib,
    pkgs,
    ...
  }: let
    cmdPackage = import ../flake-parts/cmd-package.nix {inherit lib pkgs;};
  in {
    packages = cmdPackage.mkCmdPackage {
      pname = "9p";
      description = "Read and write files on a 9p server";
      version = "0.1.0";
      binaryName = "9p-bin";
    };
  };
}
