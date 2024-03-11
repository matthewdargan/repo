{...}: {
  perSystem = {pkgs, ...}: {
    packages.gopls = pkgs.callPackage ./gopls.nix {};
  };
}
