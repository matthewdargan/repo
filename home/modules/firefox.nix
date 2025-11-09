{
  config,
  inputs,
  pkgs,
  ...
}: {
  programs.firefox = {
    enable = true;
    package = pkgs.firefox;
    profiles.${config.home.username}.extensions.packages = with inputs.firefox-addons.packages.${pkgs.system}; [
      bitwarden
      darkreader
      ublock-origin
    ];
  };
}
