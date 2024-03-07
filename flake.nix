{
  description = "home config";
  inputs = {
    nixpkgs.url = "nixpkgs/nixos-unstable";
    flake-parts.url = "github:hercules-ci/flake-parts";
    home-manager = {
      url = "github:nix-community/home-manager";
      inputs.nixpkgs.follows = "nixpkgs";
    };
    nixvim = {
      url = "github:nix-community/nixvim";
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
        modulesCommon = [inputs.nixvim.homeManagerModules.nixvim ./home/configurations/common.nix];
        modulesLinux = modulesCommon ++ [./home/configurations/linux.nix];
        modulesDarwin = modulesCommon ++ [./home/configurations/darwin.nix];
        homeConfig = {
          pkgs,
          modules,
        }:
          inputs.home-manager.lib.homeManagerConfiguration {
            inherit pkgs modules;
          };
      in {
        "mpd@win-desktop" = homeConfig {
          pkgs = pkgsLinux;
          modules = modulesLinux;
        };
        "mpd@deere-laptop" = homeConfig {
          pkgs = pkgsLinux;
          modules = modulesLinux ++ [{programs.git.userEmail = "darganmatthew@johndeere.com";}];
        };
        "mpd@mpd-macbook" = homeConfig {
          pkgs = pkgsDarwin;
          modules = modulesDarwin;
        };
        "mdargan@sai-macbook" = homeConfig {
          pkgs = pkgsDarwin;
          modules =
            modulesDarwin
            ++ [
              {
                home = {
                  homeDirectory = "/Users/mdargan";
                  username = "mdargan";
                };
              }
            ];
        };
      };
    };
}
