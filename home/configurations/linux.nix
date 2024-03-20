{pkgs, ...}: {
  home = {
    homeDirectory = "/home/mpd";
    packages = [pkgs.lutris];
  };
}
