{
  inputs,
  self,
  ...
}: {
  flake.nixosConfigurations = {
    nas = inputs.nixpkgs.lib.nixosSystem {
      modules = [
        ./nas
      ];
      specialArgs = {
        inherit inputs self;
      };
      system = "x86_64-linux";
    };
    router = inputs.nixpkgs.lib.nixosSystem {
      modules = [
        ./router
      ];
      specialArgs = {
        inherit inputs self;
      };
      system = "x86_64-linux";
    };
    scoop = inputs.nixpkgs.lib.nixosSystem {
      modules = [
        ./scoop
      ];
      specialArgs = {
        inherit inputs self;
      };
      system = "x86_64-linux";
    };
  };
}
