{
  description = "home config";
  inputs = {
    nixpkgs.url = "nixpkgs/nixos-unstable";
    flake-parts.url = "github:hercules-ci/flake-parts";
    home-manager = {
      url = "github:nix-community/home-manager";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };
  outputs = {
    self,
    flake-parts,
    ...
  } @ inputs:
    flake-parts.lib.mkFlake {inherit inputs;} {
      systems = ["x86_64-linux"];
      perSystem = {
        pkgs,
        system,
        inputs',
        ...
      }: {
        devShells.default = pkgs.mkShell {
          packages = [inputs'.home-manager.packages.home-manager];
        };
      };
      flake.homeConfigurations = let
        pkgs = inputs.nixpkgs.legacyPackages."x86_64-linux";
        homeConfig = inputs.home-manager.lib.homeManagerConfiguration {
          inherit pkgs;
          modules = [./home.nix];
        };
      in {
        "mpd@deere-laptop" = homeConfig;
        "mpd@win-desktop" = homeConfig;
      };
    };
}
