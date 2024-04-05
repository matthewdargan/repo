{inputs, ...}: {
  flake.homeConfigurations = let
    modulesCommon = [inputs.nixvim.homeManagerModules.nixvim (import ../modules/common.nix inputs)];
    modulesDarwin = modulesCommon ++ [(import ./darwin.nix inputs)];
    modulesLinux = modulesCommon ++ [./linux.nix];
    pkgsDarwin = inputs.nixpkgs.legacyPackages."aarch64-darwin";
    pkgsLinux = inputs.nixpkgs.legacyPackages."x86_64-linux";
    homeConfig = {
      modules,
      pkgs,
    }:
      inputs.home-manager.lib.homeManagerConfiguration {
        inherit modules pkgs;
      };
  in {
    "mpd@butters" = homeConfig {
      modules = modulesDarwin;
      pkgs = pkgsDarwin;
    };
    "mpd@cheese" = homeConfig {
      modules = modulesDarwin;
      pkgs = pkgsDarwin;
    };
    "mpd@deere" = homeConfig {
      modules = modulesLinux ++ [{programs.git.userEmail = "darganmatthew@johndeere.com";}];
      pkgs = pkgsLinux;
    };
    "mpd@scoop" = homeConfig {
      modules = modulesLinux ++ [{programs.git.signing.key = "E89C55C6879C7DAB";}];
      pkgs = pkgsLinux;
    };
  };
}
