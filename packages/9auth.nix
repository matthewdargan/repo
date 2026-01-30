{
  perSystem = {
    lib,
    pkgs,
    ...
  }: let
    cmdPackage = import ../flake-parts/cmd-package.nix {inherit lib pkgs;};
  in {
    packages = cmdPackage.mkCmdPackage {
      pname = "9auth";
      description = "Authentication agent for 9P with FIDO2 and Ed25519 support";
      version = "0.1.0";
      buildInputs = [pkgs.libfido2 pkgs.openssl];
      extraLinkFlags = "-lm -lfido2 -lcrypto";
    };
  };
}
