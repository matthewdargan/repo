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
      substituters = lib.mkDefault [
        "https://cache.nixos.org"
      ];
      trusted-public-keys = [
        "nas-cache:zS6WxIVuVrTFhM0lF0jZ7z6QIz48y07LlFLV7clLsIg="
        "cache.nixos.org-1:6NCHdD59X431o0gWypbMrAURkbJ16ZPMQFGspcDShjY="
      ];
      trusted-users = ["@wheel" "git"];
    };
  };
  nixpkgs.config.allowUnfree = true;
}
