{inputs, ...}: {
  flake.nixosConfigurations = {
    scoop = inputs.nixpkgs.lib.nixosSystem {
      modules = [(import ./scoop inputs)];
      system = "x86_64-linux";
    };
  };
}
