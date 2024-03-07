{
  config,
  lib,
  pkgs,
  ...
}: {
  home.homeDirectory = lib.mkDefault "/Users/mpd";
  home.username = lib.mkDefault "mpd";
}
