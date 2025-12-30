{
  perSystem = {
    lib,
    pkgs,
    ...
  }: let
    cmdPackage = import ../flake-parts/cmd-package.nix {inherit lib pkgs;};
  in {
    packages = cmdPackage.mkCmdPackage {
      pname = "authd";
      description = "Session authentication daemon for nginx auth_request";
      version = "0.1.0";
      buildInputs = [];
      extraLinkFlags = "-lm";
    };
  };
}
