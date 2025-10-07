{
  inputs,
  self,
  ...
}: {
  flake.nixosConfigurations = {
    nas = inputs.nixpkgs.lib.nixosSystem {
      modules = [
        ./nas
        ../../modules/settings.nix
      ];
      specialArgs = {
        inherit inputs self;
      };
      system = "x86_64-linux";
    };
    router = inputs.nixpkgs.lib.nixosSystem {
      modules = [
        ./router.nix
        ../../modules/settings.nix
      ];
      specialArgs = {
        inherit inputs self;
      };
      system = "x86_64-linux";
    };
    scoop = inputs.nixpkgs.lib.nixosSystem {
      modules = [
        ./scoop
        ../../modules/settings.nix
      ];
      specialArgs = {
        inherit inputs self;
      };
      system = "x86_64-linux";
    };
  };
}
