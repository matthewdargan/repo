{inputs, ...} @ part-inputs: {
  flake.homeConfigurations = let
    modulesCommon = [(import ../modules/dev.nix part-inputs) ../../modules/settings.nix];
    modulesDarwin = modulesCommon ++ [./darwin.nix ../modules/kitty.nix];
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
    "mpd@nas" = homeConfig {
      modules = modulesLinux;
      pkgs = pkgsLinux;
    };
    "mpd@scoop" = homeConfig {
      modules =
        modulesLinux
        ++ [
          (import ../modules/firefox.nix part-inputs)
          ../modules/discord.nix
          ../modules/kitty.nix
          {programs.git.signing.key = "E89C55C6879C7DAB";}
        ];
      pkgs = pkgsLinux;
    };
  };
}
