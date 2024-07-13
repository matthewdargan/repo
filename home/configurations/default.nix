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
      modules = modulesDarwin;
      pkgs = pkgsDarwin;
    };
    "mpd@cheese" = homeConfig {
      modules = modulesDarwin;
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
        ];
      pkgs = pkgsLinux;
    };
  };
}
