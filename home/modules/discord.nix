{pkgs, ...}: {
  home.packages = [
    (pkgs.vesktop.override {withSystemVencord = false;})
  ];
  xdg.desktopEntries = {
    discord = {
      exec = "vesktop";
      genericName = "Discord";
      name = "Discord";
      terminal = false;
    };
  };
}
