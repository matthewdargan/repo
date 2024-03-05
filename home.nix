{
  config,
  pkgs,
  ...
}: {
  home.username = "mpd";
  home.homeDirectory = "/home/mpd";
  home.stateVersion = "23.11";
  home.packages = [pkgs.alejandra pkgs.go_1_22 pkgs.terraform];
  nixpkgs.config.allowUnfree = true;
  programs = {
    direnv = {
      enable = true;
      enableBashIntegration = true;
      nix-direnv.enable = true;
    };
  };
}
