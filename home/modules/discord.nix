{pkgs, ...}: {
  home.packages = [
    (pkgs.vesktop.override {
      withSystemVencord = false;
    })
  ];
  xdg.desktopEntries = {
    discord = {
      name = "Discord";
      exec = "vesktop";
      genericName = "Discord";
      terminal = false;
    };
  };
}
