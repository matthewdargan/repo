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
    self.nixosModules.media-server
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
      allowedTCPPorts = [22 5640];
    };
    networkmanager.enable = true;
  };
  services = {
    "9auth" = {
      enable = true;
      authorizedUsers = ["media-server" "mpd" "storage"];
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
    media-server = {
      enable = true;
      mediaRoot = "/var/lib/media-server/n/media";
      openFirewall = true;
      mount9p = {
        dial = "tcp!127.0.0.1!5640";
        authId = "nas";
        dependsOn = ["media-serve.service"];
      };
    };
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
        after = ["network.target" "9auth.service"];
        description = "9P server for media files";
        requires = ["9auth.service"];
        wantedBy = ["multi-user.target"];

        serviceConfig = {
          # Execution
          ExecStart = "${self.packages.${pkgs.stdenv.hostPlatform.system}."9pfs"}/bin/9pfs --require-auth --auth-id=nas tcp!*!5640 /media";
          User = "storage";
          Group = "storage";

          # Lifecycle
          Restart = "always";
          RestartSec = "5s";
          TimeoutStartSec = "10s";
          TimeoutStopSec = "30s";

          # Filesystem
          RuntimeDirectory = "9pfs";
          RuntimeDirectoryMode = "0750";
          ReadWritePaths = ["/media"];

          # Security - Capabilities
          AmbientCapabilities = ["CAP_NET_BIND_SERVICE"];
          NoNewPrivileges = true;

          # Security - Filesystem Isolation
          PrivateTmp = true;
          ProtectHome = true;
          ProtectSystem = "strict";
          ProtectProc = "invisible";
          ProcSubset = "pid";

          # Security - Kernel Protection
          ProtectKernelTunables = true;
          ProtectKernelModules = true;
          ProtectKernelLogs = true;
          ProtectControlGroups = true;
          ProtectClock = true;

          # Security - Network
          RestrictAddressFamilies = ["AF_UNIX" "AF_INET" "AF_INET6"];

          # Security - System Calls
          SystemCallFilter = ["@system-service" "@file-system" "~@privileged" "~@resources"];
          SystemCallArchitectures = "native";

          # Security - Misc Restrictions
          RestrictNamespaces = true;
          RestrictRealtime = true;
          RestrictSUIDSGID = true;
          LockPersonality = true;
          MemoryDenyWriteExecute = true;
          PrivateMounts = true;
          RemoveIPC = true;
          ProtectHostname = true;
          KeyringMode = "private";
        };
      };
    };
    tmpfiles.rules = [
      "d /var/lib/git-server 0755 git git -"
    ];
  };
  system.stateVersion = "25.05";
  users = {
    groups.storage = {};
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
      storage = {
        group = "storage";
        isSystemUser = true;
      };
    };
  };
}
