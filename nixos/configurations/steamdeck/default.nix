{
  pkgs,
  self,
  ...
}: let
  user = "mpd";
in {
  imports = [
    ./boot.nix
    self.nixosModules."9auth"
    self.nixosModules."9mount"
    self.nixosModules.fish
    self.nixosModules.locale
    self.nixosModules.nix-config
  ];
  environment.systemPackages = [
    self.packages.${pkgs.stdenv.hostPlatform.system}."9p"
  ];
  networking = {
    hostName = "steamdeck";
    networkmanager.enable = true;
  };
  programs.steam.enable = true;
  services = {
    "9auth" = {
      enable = true;
      authorizedUsers = [user];
    };
    "9mount" = {
      enable = true;
      mounts = [
        {
          name = "media";
          dial = "tcp!nas!5640";
          mountPoint = "/home/${user}/n/media";
          authId = "nas";
          dependsOn = ["tailscaled.service"];
          inherit user;
        }
      ];
    };
    desktopManager.plasma6.enable = true;
    displayManager.sddm = {
      enable = true;
      wayland.enable = true;
    };
    openssh = {
      enable = true;
      settings.PermitRootLogin = "no";
    };
    pipewire = {
      enable = true;
      alsa = {
        enable = true;
        support32Bit = true;
      };
      pulse.enable = true;
    };
    tailscale.enable = true;
  };
  system.stateVersion = "26.05";
  users.users.${user} = {
    description = "Matthew Dargan";
    extraGroups = [
      "input"
      "networkmanager"
      "systemd-journal"
      "wheel"
    ];
    initialHashedPassword = "$y$j9T$GeE9dgxSIflY2YbqGPH27.$LvitWjXKM1u6jIIaXh0zDL/mbo9u9RMVQf37Omq5D8B";
    isNormalUser = true;
    openssh.authorizedKeys.keys = [
      "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAACAQDRhuefaFMT7Lzkoa2SQnRXl7UcLabk75Wxa2MVi4j37Z9gTr2u8oYXNWTTaGZ80IDICvsLczj+f1kEx2zXp8qY1VoOvXye1kRuQYwRhW+sW5QGz2KtXzNCK71bfChNc8xyvyqEg1c2JfKL/clLVMh47tq46TYt2x9RDFtE2vlQKf7tKmsgdyEJJYkxHhUbNe5WPlC/B9ForIdJO1CuwB7qfA8FB1l+Gw24j5SKkJgm+2Zn91GF0u6/eDlZSAFAOF61XAtmcZmpZdXBCNVkfEO+P0/7638BpEs4I6MSevKo97xwaDkXWpEXV3O7mZjbiwGdEtrRXLSPiOh5VnAykavvP6O8T37gO7FstdAIxDTWu2PerxPz5iX93A5bKGKD7TDPBHLjWxez1j+fhIgjKRicQXgUAt1RjOpZruL3/5DucG+GrjScW/jUtPV6mvuOsLguEIkXyfO0xgC6EzFKtKOmmnH6NjUspZ5eoPgdCQ7GLukksERY6bWm8N8pFy+SmUER1TC0V42ur1nMaTwlGkYJonOpe0BYQJRccukzxSSJNs+079Adqj1VCFY0QG1172i1KTmou34BhM+ZdKqrutsBWAoeO9aN2/7KnL9bPDIYHBDJEawspt+RBMb83dlm7rev0IlpsA8RxCkf+QNqYWjZzU3vX1Xn47r9136HYDaWAQ== openpgp:0xE9E3F438"
    ];
    shell = pkgs.fish;
  };
}
