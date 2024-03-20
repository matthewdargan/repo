{inputs, ...}: {
  flake.homeConfigurations = let
    pkgsLinux = inputs.nixpkgs.legacyPackages."x86_64-linux";
    pkgsDarwin = inputs.nixpkgs.legacyPackages."aarch64-darwin";
    modulesCommon = [inputs.nixvim.homeManagerModules.nixvim (import ../modules/common.nix inputs)];
    modulesLinux = modulesCommon ++ [./linux.nix];
    modulesDarwin = modulesCommon ++ [(import ./darwin.nix inputs)];
    homeConfig = {
      pkgs,
      modules,
    }:
      inputs.home-manager.lib.homeManagerConfiguration {
        inherit pkgs modules;
      };
  in {
    "mpd@scoop" = homeConfig {
      pkgs = pkgsLinux;
      modules = modulesLinux;
    };
    "mpd@deere" = homeConfig {
      pkgs = pkgsLinux;
      modules = modulesLinux ++ [{programs.git.userEmail = "darganmatthew@johndeere.com";}];
    };
    "mpd@cheese" = homeConfig {
      pkgs = pkgsDarwin;
      modules = modulesDarwin;
    };
    "mpd@butters" = homeConfig {
      pkgs = pkgsDarwin;
      modules = modulesDarwin;
    };
  };
}
