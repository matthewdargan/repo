{inputs, ...}: {pkgs, ...}: {
  home.packages = [
    inputs.epify.packages.${pkgs.system}.epify
    inputs.mooch.packages.${pkgs.system}.mooch
    pkgs.rain-bittorrent
  ];
}
