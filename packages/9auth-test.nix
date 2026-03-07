{
  perSystem = {
    lib,
    pkgs,
    ...
  }: let
    cmdPackage = import ../flake-parts/cmd-package.nix {inherit lib pkgs;};
  in {
    packages = cmdPackage.mkCmdPackage {
      pname = "9auth-test";
      description = "9auth test suite";
      version = "0.1.0";
      buildInputs = [pkgs.libfido2 pkgs.openssl pkgs.tpm2-tss];
      extraLinkFlags = "-lfido2 -lcrypto -ltss2-esys -ltss2-mu";
    };
  };
}
