{
  inputs,
  self,
  ...
}: {
  flake.homeConfigurations = let
    pkgs = inputs.nixpkgs.legacyPackages."x86_64-linux";
    baseModules = [
      self.homeModules.base
      self.homeModules.development
    ];
    extraSpecialArgs = {
      inherit inputs self;
    };
  in {
    "mpd@nas" = inputs.home-manager.lib.homeManagerConfiguration {
      inherit extraSpecialArgs pkgs;
      modules = baseModules;
    };

    "mpd@scoop" = inputs.home-manager.lib.homeManagerConfiguration {
      inherit extraSpecialArgs pkgs;
      modules =
        baseModules
        ++ [
          self.homeModules.discord
          self.homeModules.firefox
          self.homeModules.ghostty
        ];
    };
  };
}
