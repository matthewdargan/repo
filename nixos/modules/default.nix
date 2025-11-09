_: {
  flake.nixosModules = {
    "9p-tools" = ./9p-tools.nix;
    "fish" = ./fish.nix;
    "locale" = ./locale.nix;
    "nix-config" = ./nix-config.nix;
    "settings" = ./settings.nix;
  };
}
