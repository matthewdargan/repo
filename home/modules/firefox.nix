{inputs, ...}: {
  config,
  pkgs,
  ...
}: {
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
}
