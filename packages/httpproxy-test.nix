{
  perSystem = {
    lib,
    pkgs,
    ...
  }: let
    cmdPackage = import ../flake-parts/cmd-package.nix {inherit lib pkgs;};
  in {
    packages = cmdPackage.mkCmdPackage {
      pname = "httpproxy-test";
      description = "HTTP proxy test suite";
      version = "0.1.0";
    };
  };
}
