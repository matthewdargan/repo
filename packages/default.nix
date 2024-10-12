{
  imports = [./neovim ./rain];
  perSystem = {pkgs, ...}: {
    packages.plan9port = pkgs.callPackage ./plan9port {};
  };
}
