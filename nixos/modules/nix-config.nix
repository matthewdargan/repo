{inputs, ...}: {
  nix = {
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
      trusted-users = ["@wheel"];
    };
  };
  nixpkgs.config.allowUnfree = true;
}
