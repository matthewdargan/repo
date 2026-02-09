_: {
  flake.nixosModules = {
    "9auth" = ./9auth.nix;
    "9mount" = ./9mount.nix;
    "9p-tools" = ./9p-tools.nix;
    "fish" = ./fish.nix;
    "git-server" = ./git-server.nix;
    "locale" = ./locale.nix;
    "nginx" = ./nginx.nix;
    "nix-config" = ./nix-config.nix;
    "yubikey" = ./yubikey.nix;
  };
}
