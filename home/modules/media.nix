{
  inputs,
  self,
  ...
}: {pkgs, ...}: {
  home.packages = [
    inputs.epify.packages.${pkgs.system}.epify
    self.packages.${pkgs.system}.tor
    self.packages.${pkgs.system}.tordl
    self.packages.${pkgs.system}.torrss
    pkgs.rain-bittorrent
  ];
}
