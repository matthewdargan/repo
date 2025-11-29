{
  inputs,
  lib,
  pkgs,
  ...
}: {
  nix = {
    package = pkgs.nix;
    gc = {
      automatic = true;
      options = "--delete-older-than 5d";
    };
    nixPath = ["nixpkgs=${inputs.nixpkgs}"];
    registry.nixpkgs.flake = inputs.nixpkgs;
    settings = {
      auto-optimise-store = true;
      experimental-features = [
        "nix-command"
        "flakes"
      ];
      require-sigs = false;
      substituters = lib.mkForce [
        "file:///n/nix"
        "https://cache.nixos.org"
        "https://nix-community.cachix.org"
      ];
      trusted-public-keys = [
        "cache.nixos.org-1:6NCHdD59X431o0gWypbMrAURkbJ16ZPMQFGspcDShjY="
        "nix-community.cachix.org-1:mB9FSh9qf2dCimDSUo8Zy7bkq5CX+/rkCWyvRCYg3Fs="
      ];
      trusted-substituters = ["file:///n/nix"];
      trusted-users = ["@wheel"];
    };
  };
  nixpkgs.config.allowUnfree = true;
}
