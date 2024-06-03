{inputs, ...}: {
  flake.nixosConfigurations = {
    nas = inputs.nixpkgs.lib.nixosSystem {
      modules = [(import ./nas inputs) ../../modules/settings.nix];
      system = "x86_64-linux";
    };
    scoop = inputs.nixpkgs.lib.nixosSystem {
      modules = [(import ./scoop inputs) ../../modules/settings.nix];
      system = "x86_64-linux";
    };
  };
}
