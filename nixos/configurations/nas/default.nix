{
  config,
  pkgs,
  self,
  ...
}: {
  imports = [
    ./hardware.nix
    self.nixosModules."9auth"
    self.nixosModules."9mount"
    self.nixosModules.fish
    self.nixosModules.git-server
    self.nixosModules.locale
    self.nixosModules.nix-config
  ];
  boot = {
    loader = {
      efi.canTouchEfiVariables = true;
      systemd-boot.enable = true;
    };
    supportedFilesystems = ["btrfs" "ext4" "vfat"];
  };
  environment.systemPackages = [
    self.packages.${pkgs.stdenv.hostPlatform.system}."9p"
  ];
  networking = rec {
    hostId = builtins.substring 0 8 (builtins.hashString "md5" hostName);
    hostName = "nas";
    firewall.interfaces.${config.services.tailscale.interfaceName} = {
      allowedTCPPorts = [22 5640 8080 8096];
      allowedUDPPorts = [8096];
    };
    networkmanager.enable = true;
  };
  services = {
    "9auth" = {
      enable = true;
      authorizedUsers = ["jellyfin" "media-server" "mpd" "storage"];
    };
    "9mount" = {
      enable = true;
      mounts = [
        {
          name = "media";
          dial = "tcp!127.0.0.1!5640";
          mountPoint = "/var/lib/jellyfin/n/media";
          authId = "nas";
          dependsOn = ["media-serve.service"];
          user = "jellyfin";
        }
        {
          name = "media-server";
          dial = "tcp!127.0.0.1!5640";
          mountPoint = "/var/lib/media-server/n/media";
          authId = "nas";
          dependsOn = ["media-serve.service"];
          user = "media-server";
        }
      ];
    };
    btrfs.autoScrub.enable = true;
    git-server = {
      enable = true;
      baseDir = "/srv/git";
      extraGroups = ["storage"];
      authorizedKeys = [
        "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAACAQDRhuefaFMT7Lzkoa2SQnRXl7UcLabk75Wxa2MVi4j37Z9gTr2u8oYXNWTTaGZ80IDICvsLczj+f1kEx2zXp8qY1VoOvXye1kRuQYwRhW+sW5QGz2KtXzNCK71bfChNc8xyvyqEg1c2JfKL/clLVMh47tq46TYt2x9RDFtE2vlQKf7tKmsgdyEJJYkxHhUbNe5WPlC/B9ForIdJO1CuwB7qfA8FB1l+Gw24j5SKkJgm+2Zn91GF0u6/eDlZSAFAOF61XAtmcZmpZdXBCNVkfEO+P0/7638BpEs4I6MSevKo97xwaDkXWpEXV3O7mZjbiwGdEtrRXLSPiOh5VnAykavvP6O8T37gO7FstdAIxDTWu2PerxPz5iX93A5bKGKD7TDPBHLjWxez1j+fhIgjKRicQXgUAt1RjOpZruL3/5DucG+GrjScW/jUtPV6mvuOsLguEIkXyfO0xgC6EzFKtKOmmnH6NjUspZ5eoPgdCQ7GLukksERY6bWm8N8pFy+SmUER1TC0V42ur1nMaTwlGkYJonOpe0BYQJRccukzxSSJNs+079Adqj1VCFY0QG1172i1KTmou34BhM+ZdKqrutsBWAoeO9aN2/7KnL9bPDIYHBDJEawspt+RBMb83dlm7rev0IlpsA8RxCkf+QNqYWjZzU3vX1Xn47r9136HYDaWAQ== openpgp:0xE9E3F438"
      ];
      repositories.repo.postReceiveHook = pkgs.writeShellScript "post-receive" ''
        set -euo pipefail
        exec ${pkgs.git}/bin/git push --mirror github 2>&1 || true
      '';
    };
    jellyfin.enable = true;
    openssh = {
      enable = true;
      settings.PermitRootLogin = "no";
    };
    tailscale.enable = true;
  };
  systemd = {
    services = {
      # 9P servers
      media-serve = {
        after = ["network.target"];
        description = "9P server for media files";
        serviceConfig = {
          ExecStart = "${self.packages.${pkgs.stdenv.hostPlatform.system}."9pfs"}/bin/9pfs --root=/media --auth-id=nas tcp!*!5640";
          Restart = "always";
          RestartSec = "5s";
          User = "storage";
        };
        wantedBy = ["multi-user.target"];
      };
      media-server = {
        after = ["9mount-media-server.service"];
        description = "DASH media server with FFmpeg integration";
        serviceConfig = {
          ExecStart = "${self.packages.${pkgs.stdenv.hostPlatform.system}.media-server}/bin/media-server --media-root=/var/lib/media-server/n/media";
          Restart = "always";
          RestartSec = "5s";
          User = "media-server";
        };
        wants = ["9mount-media-server.service"];
        wantedBy = ["multi-user.target"];
      };
      jellyfin = {
        after = ["9mount-media.service"];
        wants = ["9mount-media.service"];
      };
    };
    tmpfiles.rules = [
      "d /var/lib/git-server 0755 git git -"
      "d /var/lib/jellyfin/n 0755 jellyfin jellyfin -"
      "d /var/lib/media-server/n 0755 media-server media-server -"
    ];
  };
  system.stateVersion = "25.05";
  users = {
    groups = {
      media-server = {};
      storage = {};
    };
    users = {
      mpd = {
        description = "Matthew Dargan";
        extraGroups = ["input" "networkmanager" "systemd-journal" "wheel"];
        isNormalUser = true;
        openssh.authorizedKeys.keys = [
          "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAACAQDRhuefaFMT7Lzkoa2SQnRXl7UcLabk75Wxa2MVi4j37Z9gTr2u8oYXNWTTaGZ80IDICvsLczj+f1kEx2zXp8qY1VoOvXye1kRuQYwRhW+sW5QGz2KtXzNCK71bfChNc8xyvyqEg1c2JfKL/clLVMh47tq46TYt2x9RDFtE2vlQKf7tKmsgdyEJJYkxHhUbNe5WPlC/B9ForIdJO1CuwB7qfA8FB1l+Gw24j5SKkJgm+2Zn91GF0u6/eDlZSAFAOF61XAtmcZmpZdXBCNVkfEO+P0/7638BpEs4I6MSevKo97xwaDkXWpEXV3O7mZjbiwGdEtrRXLSPiOh5VnAykavvP6O8T37gO7FstdAIxDTWu2PerxPz5iX93A5bKGKD7TDPBHLjWxez1j+fhIgjKRicQXgUAt1RjOpZruL3/5DucG+GrjScW/jUtPV6mvuOsLguEIkXyfO0xgC6EzFKtKOmmnH6NjUspZ5eoPgdCQ7GLukksERY6bWm8N8pFy+SmUER1TC0V42ur1nMaTwlGkYJonOpe0BYQJRccukzxSSJNs+079Adqj1VCFY0QG1172i1KTmou34BhM+ZdKqrutsBWAoeO9aN2/7KnL9bPDIYHBDJEawspt+RBMb83dlm7rev0IlpsA8RxCkf+QNqYWjZzU3vX1Xn47r9136HYDaWAQ== openpgp:0xE9E3F438"
        ];
        shell = pkgs.fish;
      };
      media-server = {
        group = "media-server";
        isSystemUser = true;
      };
      storage = {
        group = "storage";
        isSystemUser = true;
      };
    };
  };
}
