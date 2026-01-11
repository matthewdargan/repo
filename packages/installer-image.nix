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
            "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAACAQDRhuefaFMT7Lzkoa2SQnRXl7UcLabk75Wxa2MVi4j37Z9gTr2u8oYXNWTTaGZ80IDICvsLczj+f1kEx2zXp8qY1VoOvXye1kRuQYwRhW+sW5QGz2KtXzNCK71bfChNc8xyvyqEg1c2JfKL/clLVMh47tq46TYt2x9RDFtE2vlQKf7tKmsgdyEJJYkxHhUbNe5WPlC/B9ForIdJO1CuwB7qfA8FB1l+Gw24j5SKkJgm+2Zn91GF0u6/eDlZSAFAOF61XAtmcZmpZdXBCNVkfEO+P0/7638BpEs4I6MSevKo97xwaDkXWpEXV3O7mZjbiwGdEtrRXLSPiOh5VnAykavvP6O8T37gO7FstdAIxDTWu2PerxPz5iX93A5bKGKD7TDPBHLjWxez1j+fhIgjKRicQXgUAt1RjOpZruL3/5DucG+GrjScW/jUtPV6mvuOsLguEIkXyfO0xgC6EzFKtKOmmnH6NjUspZ5eoPgdCQ7GLukksERY6bWm8N8pFy+SmUER1TC0V42ur1nMaTwlGkYJonOpe0BYQJRccukzxSSJNs+079Adqj1VCFY0QG1172i1KTmou34BhM+ZdKqrutsBWAoeO9aN2/7KnL9bPDIYHBDJEawspt+RBMb83dlm7rev0IlpsA8RxCkf+QNqYWjZzU3vX1Xn47r9136HYDaWAQ== openpgp:0xE9E3F438"
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
