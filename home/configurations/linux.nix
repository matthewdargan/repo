{pkgs, ...}: {
  home = {
    homeDirectory = "/home/mpd";
    packages = [pkgs.lutris];
  };
  services.gpg-agent = {
    enable = true;
    pinentryPackage = pkgs.pinentry-tty;
  };
}
