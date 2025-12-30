_: {
  flake.nixosModules = {
    "9p-health-check" = ./9p-health-check.nix;
    "9p-tools" = ./9p-tools.nix;
    "fish" = ./fish.nix;
    "git-server" = ./git-server.nix;
    "locale" = ./locale.nix;
    "nginx" = ./nginx.nix;
    "nix-client" = ./nix-client.nix;
    "nix-config" = ./nix-config.nix;
  };
}
