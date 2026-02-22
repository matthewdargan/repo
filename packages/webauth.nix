{
  perSystem = {
    lib,
    pkgs,
    ...
  }: let
    cmdPackage = import ../flake-parts/cmd-package.nix {inherit lib pkgs;};
  in {
    packages = cmdPackage.mkCmdPackage {
      pname = "webauth";
      description = "WebAuthn authentication daemon with 9P bridge to 9auth";
      version = "0.1.0";
      buildInputs = [pkgs.libfido2 pkgs.openssl];
      extraLinkFlags = "-lfido2 -lcrypto -lm";
    };
  };
}
