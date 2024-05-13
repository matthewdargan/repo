{inputs, ...}: {
  config,
  pkgs,
  ...
}: {
  home.homeDirectory = "/home/mpd";
  programs.firefox = {
    enable = true;
    package = pkgs.firefox;
    profiles.${config.home.username}.extensions = with inputs.firefox-addons.packages.${pkgs.system}; [
      bitwarden
      darkreader
      refined-github
      ublock-origin
    ];
  };
  services.gpg-agent = {
    enable = true;
    pinentryPackage = pkgs.pinentry-tty;
  };
}
