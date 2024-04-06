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
      modules = modulesDarwin ++ [{programs.git.signing.key = "A2040D8058F5C0A5";}];
      pkgs = pkgsDarwin;
    };
    "mpd@cheese" = homeConfig {
      modules = modulesDarwin ++ [{programs.git.signing.key = "347727B6166BBB04";}];
      pkgs = pkgsDarwin;
    };
    "mpd@deere" = homeConfig {
      modules =
        modulesLinux
        ++ [
          {
            programs.git = {
              signing.key = "524E1845B7FD26B1";
              userEmail = "darganmatthew@johndeere.com";
            };
          }
        ];
      pkgs = pkgsLinux;
    };
    "mpd@scoop" = homeConfig {
      modules = modulesLinux ++ [{programs.git.signing.key = "E89C55C6879C7DAB";}];
      pkgs = pkgsLinux;
    };
  };
}
