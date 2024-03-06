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
      systems = ["x86_64-linux" "aarch64-darwin"];
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
        pkgsLinux = inputs.nixpkgs.legacyPackages."x86_64-linux";
        pkgsDarwin = inputs.nixpkgs.legacyPackages."aarch64-darwin";
        homeConfig = pkgs: inputs.home-manager.lib.homeManagerConfiguration {
            inherit pkgs;
            modules = [./home.nix];
        };
      in {
        "mpd@deere-laptop" = homeConfig pkgsLinux;
        "mpd@win-desktop" = homeConfig pkgsLinux;
        "mdargan@sai-macbook" = homeConfig pkgsDarwin;
      };
    };
}
