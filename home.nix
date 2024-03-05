{
  config,
  pkgs,
  ...
}: {
  home.username = "mpd";
  home.homeDirectory = "/home/mpd";
  home.stateVersion = "23.11";
  home.packages = [pkgs.go_1_22 pkgs.terraform pkgs.alejandra];
  nixpkgs.config.allowUnfree = true;
}
