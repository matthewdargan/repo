{
  config,
  pkgs,
  self,
  ...
}: {
  imports = [
    ./hardware.nix
    self.nixosModules."9p-tools"
    self.nixosModules.fish
    self.nixosModules.locale
    self.nixosModules.nix-config
    self.nixosModules.settings
  ];
  boot = {
    loader = {
      efi.canTouchEfiVariables = true;
      systemd-boot.enable = true;
    };
    supportedFilesystems = [
      "btrfs"
      "ext4"
      "vfat"
    ];
  };
  networking = rec {
    hostId = builtins.substring 0 8 (builtins.hashString "md5" hostName);
    hostName = "nas";
    firewall = {
      allowedTCPPorts = [4500];
      interfaces.${config.services.tailscale.interfaceName}.allowedTCPPorts = [
        22
        7246
        8096
      ];
    };
    networkmanager.enable = true;
  };
  services = {
    btrfs.autoScrub.enable = true;
    jellyfin = {
      enable = true;
      cacheDir = "/home/media/.cache/jellyfin";
      dataDir = "/home/media/jellyfin";
      openFirewall = true;
      user = "media";
    };
    openssh = {
      enable = true;
      settings.PermitRootLogin = "no";
    };
    tailscale.enable = true;
  };
  systemd = {
    services = {
      jellyfin = {
        after = ["nas-mount.service"];
        wants = ["nas-mount.service"];
      };
      "nas-mount" = {
        after = [
          "network-online.target"
          "9pfs.service"
        ];
        description = "mount nas";
        serviceConfig = {
          ExecStart = [
            ''/bin/sh -c "if ${pkgs.util-linux}/bin/mountpoint -q /home/media/n/nas; then /run/wrappers/bin/9umount /home/media/n/nas; fi"''
            ''/bin/sh -c "if ${pkgs.util-linux}/bin/mountpoint -q /home/media/n/movies; then /run/wrappers/bin/9umount /home/media/n/movies; fi"''
            ''/bin/sh -c "if ${pkgs.util-linux}/bin/mountpoint -q /home/media/n/shows; then /run/wrappers/bin/9umount /home/media/n/shows; fi"''
            "/run/wrappers/bin/9mount 'tcp!nas!4500' /home/media/n/nas"
            "/run/wrappers/bin/9bind /home/media/n/nas/movies /home/media/n/movies"
            "/run/wrappers/bin/9bind /home/media/n/nas/shows /home/media/n/shows"
          ];
          ExecStartPre = [
            "${pkgs.coreutils}/bin/mkdir -p /home/media/n/nas /home/media/n/movies /home/media/n/shows"
          ];
          RemainAfterExit = true;
          Restart = "on-failure";
          RestartSec = "5s";
          Type = "oneshot";
          User = "media";
        };
        wantedBy = ["multi-user.target"];
        wants = [
          "network-online.target"
          "9pfs.service"
        ];
      };
      "9pfs" = {
        after = ["network.target"];
        description = "9P filesystem server (debug)";
        serviceConfig = {
          ExecStart = "${self.packages.${pkgs.stdenv.hostPlatform.system}."9pfs-debug"}/bin/9pfs --root=/media tcp!*!4500";
          Restart = "always";
          RestartSec = "5s";
          StandardError = "journal";
          StandardOutput = "journal";
          User = "storage";
        };
        wantedBy = ["multi-user.target"];
      };
    };
  };
  system.stateVersion = "25.05";
  users = {
    groups.media = {};
    groups.storage = {};
    users = {
      media = {
        extraGroups = ["systemd-journal"];
        group = "media";
        isNormalUser = true;
        linger = true;
        packages = [self.packages.${pkgs.stdenv.hostPlatform.system}.neovim];
        shell = pkgs.fish;
      };
      mpd = {
        description = "Matthew Dargan";
        extraGroups = [
          "input"
          "networkmanager"
          "systemd-journal"
          "wheel"
        ];
        isNormalUser = true;
        openssh.authorizedKeys.keys = [
          "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAILe/v2phdFJcaINc1bphWEM6vXDSlXY/e0B2zyb3ik1M matthewdargan57@gmail.com"
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
