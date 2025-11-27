_: {
  flake.nixosModules = {
    "9p-tools" = ./9p-tools.nix;
    "fish" = ./fish.nix;
    "git-server" = ./git-server.nix;
    "locale" = ./locale.nix;
    "nix-cache-client" = ./nix-cache-client.nix;
    "nix-config" = ./nix-config.nix;
    "settings" = ./settings.nix;
  };
}
