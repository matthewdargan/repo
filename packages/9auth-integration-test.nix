{
  perSystem = {
    lib,
    pkgs,
    ...
  }: let
    cmdPackage = import ../flake-parts/cmd-package.nix {inherit lib pkgs;};
  in {
    packages = cmdPackage.mkCmdPackage {
      pname = "9auth-integration-test";
      description = "9auth integration test suite";
      version = "0.1.0";
      buildInputs = [pkgs.libfido2];
    };
  };
}
