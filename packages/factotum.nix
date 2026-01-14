{
  perSystem = {
    lib,
    pkgs,
    ...
  }: let
    cmdPackage = import ../flake-parts/cmd-package.nix {inherit lib pkgs;};
  in {
    packages = cmdPackage.mkCmdPackage {
      pname = "factotum";
      description = "Authentication agent for 9P with FIDO2/Yubikey support";
      version = "0.1.0";
      buildInputs = [];
      extraLinkFlags = "-lm";
    };
  };
}
