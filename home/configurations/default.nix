{
  inputs,
  self,
  ...
}: let
  extraSpecialArgs = {inherit inputs self;};
  pkgs = inputs.nixpkgs.legacyPackages."x86_64-linux";
  mkHome = modules:
    inputs.home-manager.lib.homeManagerConfiguration {
      inherit extraSpecialArgs modules pkgs;
    };
in {
  flake.homeConfigurations = {
    "mpd@nas" = mkHome [
      self.homeModules.base
      self.homeModules.development
    ];

    "mpd@scoop" = mkHome [
      self.homeModules.agents
      self.homeModules.base
      self.homeModules.development
      self.homeModules.discord
      self.homeModules.firefox
      self.homeModules.ghostty
      self.homeModules.gpg
      self.homeModules.jellyfin
      self.homeModules.retroarch
    ];

    "mpd@steamdeck" = mkHome [
      self.homeModules.base
      self.homeModules.firefox
      self.homeModules.ghostty
      self.homeModules.jellyfin
      self.homeModules.retroarch
    ];
  };
}
