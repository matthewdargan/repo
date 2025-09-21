{inputs, ...} @ part-inputs: {
  flake.homeConfigurations = let
    modulesLinux = [
      (import ../modules/dev.nix part-inputs)
      ./linux.nix
      ../../modules/settings.nix
    ];
    pkgsLinux = inputs.nixpkgs.legacyPackages."x86_64-linux";
    homeConfig = {
      modules,
      pkgs,
    }:
      inputs.home-manager.lib.homeManagerConfiguration {
        inherit modules pkgs;
      };
  in {
    "mpd@nas" = homeConfig {
      modules = modulesLinux ++ [(import ../modules/media.nix part-inputs)];
      pkgs = pkgsLinux;
    };
    "mpd@scoop" = homeConfig {
      modules =
        modulesLinux
        ++ [
          (import ../modules/firefox.nix part-inputs)
          (import ../modules/ghostty.nix part-inputs)
          ../modules/discord.nix
        ];
      pkgs = pkgsLinux;
    };
  };
}
