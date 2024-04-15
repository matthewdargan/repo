{inputs, ...}: {
  flake.nixosConfigurations = {
    scoop = inputs.nixpkgs.lib.nixosSystem {
      modules = [(import ./scoop inputs) ../../modules/settings.nix];
      system = "x86_64-linux";
    };
  };
}
