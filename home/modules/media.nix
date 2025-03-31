{inputs, ...}: {pkgs, ...}: {
  home.packages = [
    inputs.epify.packages.${pkgs.system}.epify
    inputs.media-server.packages.${pkgs.system}.mooch
    pkgs.rain-bittorrent
  ];
}
