{
  perSystem = {
    lib,
    pkgs,
    ...
  }: let
    cmdPackage = import ../flake-parts/cmd-package.nix {inherit lib pkgs;};
  in {
    packages = cmdPackage.mkCmdPackage {
      pname = "httpproxy";
      description = "HTTP reverse proxy with TLS termination";
      version = "0.1.0";
      buildInputs = [pkgs.openssl pkgs.nghttp2];
      extraLinkFlags = "-lm -lssl -lcrypto -lnghttp2";
    };
  };
}
