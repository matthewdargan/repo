{inputs, ...}: {
  flake.homeConfigurations = let
    pkgsLinux = inputs.nixpkgs.legacyPackages."x86_64-linux";
    pkgsDarwin = inputs.nixpkgs.legacyPackages."aarch64-darwin";
    modulesCommon = [inputs.nixvim.homeManagerModules.nixvim (import ../modules/common.nix inputs)];
    modulesLinux = modulesCommon ++ [./linux.nix];
    modulesDarwin = modulesCommon ++ [./darwin.nix];
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
}
