{inputs, ...}: {pkgs, ...}: {
  home.packages = [
    inputs.epify.packages.${pkgs.system}.epify
    inputs.media-server.packages.${pkgs.system}.tor
    inputs.media-server.packages.${pkgs.system}.tordl
    inputs.media-server.packages.${pkgs.system}.torrss
    pkgs.rain-bittorrent
  ];
}
