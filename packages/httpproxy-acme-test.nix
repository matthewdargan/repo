{
  perSystem = {
    lib,
    pkgs,
    ...
  }: let
    cmdPackage = import ../flake-parts/cmd-package.nix {inherit lib pkgs;};
  in {
    packages = cmdPackage.mkCmdPackage {
      pname = "httpproxy-acme-test";
      description = "Test suite for ACME implementation in httpproxy";
      version = "0.1.0";
      buildInputs = [pkgs.openssl];
      extraLinkFlags = "-lm -lssl -lcrypto";
    };
  };
}
