{inputs, ...}: {pkgs, ...}: {
  home.packages = [
    inputs.epify.packages.${pkgs.system}.epify
    inputs.src.packages.${pkgs.system}.tor
    inputs.src.packages.${pkgs.system}.tordl
    inputs.src.packages.${pkgs.system}.torrss
    pkgs.rain-bittorrent
  ];
}
