{pkgs, ...}: {
  home.homeDirectory = "/home/mpd";
  services.gpg-agent = {
    enable = true;
    pinentryPackage = pkgs.pinentry-tty;
  };
}
