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
      description = "9auth integration test suite (Ed25519 + FIDO2)";
      version = "0.1.0";
      buildInputs = [pkgs.libfido2 pkgs.openssl];
      extraLinkFlags = "-lm -lfido2 -lcrypto";
    };
  };
}
