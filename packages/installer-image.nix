{
  inputs,
  self,
  ...
}: {
  perSystem = {
    lib,
    system,
    self',
    ...
  }: let
    extraPackages.environment.systemPackages = [
      self'.packages.neovim
    ];
    common = [
      self.nixosModules.locale
      {
        networking = rec {
          hostId = builtins.substring 0 8 (builtins.hashString "md5" hostName);
          hostName = "nixos-installer";
        };
        nix.settings = {
          experimental-features = [
            "nix-command"
            "flakes"
          ];
          trusted-users = ["@wheel"];
        };
        system.configurationRevision = self.rev or "dirty";
        users.users.nixos = {
          initialHashedPassword = lib.mkForce "$y$j9T$uLeBfLXoVPfbUxsixAlFB1$OPaY5QGC1wCPg80iIU1ITv1E4EC65nTqvOwoeyis900";
          openssh.authorizedKeys.keys = [
            "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAILe/v2phdFJcaINc1bphWEM6vXDSlXY/e0B2zyb3ik1M matthewdargan57@gmail.com"
          ];
        };
      }
    ];
    minimal = inputs.nixpkgs.lib.nixosSystem {
      inherit system;
      modules =
        [
          "${inputs.nixpkgs}/nixos/modules/installer/cd-dvd/installation-cd-minimal.nix"
          extraPackages
        ]
        ++ common;
    };
  in {
    packages."installer/minimal" = minimal.config.system.build.isoImage;
  };
}
