{inputs, ...} @ part-inputs: {
  flake.homeConfigurations = let
    modulesCommon = [(import ../modules/dev.nix part-inputs) ../../modules/settings.nix];
    modulesDarwin = modulesCommon ++ [./darwin.nix ../modules/kitty.nix];
    modulesLinux = modulesCommon ++ [(import ./linux.nix part-inputs)];
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
            programs = {
              git = {
                signing.key = "524E1845B7FD26B1";
                userEmail = "darganmatthew@johndeere.com";
              };
              vscode.enable = false;
            };
          }
        ];
      pkgs = pkgsLinux;
    };
    "mpd@scoop" = homeConfig {
      modules =
        modulesLinux
        ++ [
          ../modules/gaming.nix
          ../modules/kitty.nix
          {programs.git.signing.key = "E89C55C6879C7DAB";}
        ];
      pkgs = pkgsLinux;
    };
  };
}
