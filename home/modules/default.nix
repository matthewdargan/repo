_: {
  flake.homeModules = {
    "base" = ./base.nix;
    "development" = ./development.nix;
    "discord" = ./discord.nix;
    "firefox" = ./firefox.nix;
    "ghostty" = ./ghostty.nix;
    "jellyfin" = ./jellyfin.nix;
    "retroarch" = ./retroarch.nix;
  };
}
