{
  nixpkgs,
  self,
  src,
  u9fs,
  ...
}: {
  config,
  pkgs,
  ...
}: {
  imports = [./hardware.nix];
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
  i18n = {
    defaultLocale = "en_US.UTF-8";
    extraLocaleSettings = {
      LC_ADDRESS = "en_US.UTF-8";
      LC_IDENTIFICATION = "en_US.UTF-8";
      LC_MEASUREMENT = "en_US.UTF-8";
      LC_MONETARY = "en_US.UTF-8";
      LC_NAME = "en_US.UTF-8";
      LC_NUMERIC = "en_US.UTF-8";
      LC_PAPER = "en_US.UTF-8";
      LC_TELEPHONE = "en_US.UTF-8";
      LC_TIME = "en_US.UTF-8";
    };
  };
  networking = rec {
    hostId = builtins.substring 0 8 (builtins.hashString "md5" hostName);
    hostName = "nas";
    firewall = {
      allowedTCPPorts = [
        22
        4500
      ];
      checkReversePath = "loose";
      interfaces.${config.services.tailscale.interfaceName}.allowedTCPPorts = [
        7246
        8096
      ];
    };
    networkmanager.enable = true;
  };
  nix = {
    gc = {
      automatic = true;
      options = "--delete-older-than 5d";
    };
    nixPath = ["nixpkgs=${nixpkgs}"];
    registry.nixpkgs.flake = nixpkgs;
    settings = {
      auto-optimise-store = true;
      experimental-features = "nix-command flakes";
      trusted-users = ["@wheel"];
    };
  };
  nixpkgs.config.allowUnfree = true;
  programs.fish.enable = true;
  security.wrappers = {
    "9bind" = {
      owner = "root";
      group = "root";
      permissions = "u+rx,g+x,o+x";
      setuid = true;
      source = "${src.packages.${pkgs.system}."9bind"}/bin/9bind";
    };
    "9mount" = {
      owner = "root";
      group = "root";
      permissions = "u+rx,g+x,o+x";
      setuid = true;
      source = "${src.packages.${pkgs.system}."9mount"}/bin/9mount";
    };
    "9umount" = {
      owner = "root";
      group = "root";
      permissions = "u+rx,g+x,o+x";
      setuid = true;
      source = "${src.packages.${pkgs.system}."9umount"}/bin/9umount";
    };
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
  system.stateVersion = "25.05";
  systemd = {
    services = {
      "nas-mount" = {
        after = ["network.target"];
        description = "mount nas";
        serviceConfig = {
          ExecStart = [
            "/run/wrappers/bin/9mount 'tcp!nas!4500' /home/media/n/nas"
            "/run/wrappers/bin/9bind /home/media/n/nas/movies /home/media/n/movies"
            "/run/wrappers/bin/9bind /home/media/n/nas/shows /home/media/n/shows"
          ];
          ExecStartPre = [
            "${pkgs.coreutils}/bin/mkdir -p /home/media/n/nas /home/media/n/movies /home/media/n/shows"
          ];
          ExecStop = [
            "/run/wrappers/bin/9umount /home/media/n/nas /home/media/n/movies /home/media/n/shows"
          ];
          RemainAfterExit = true;
          Type = "oneshot";
          User = "media";
        };
        wantedBy = ["multi-user.target"];
      };
      "u9fs@" = {
        after = ["network.target"];
        description = "9P filesystem server";
        serviceConfig = {
          ExecStart = "${u9fs.packages.${pkgs.system}.u9fs}/bin/u9fs -D -a none -u storage -d /media";
          StandardError = "journal";
          StandardInput = "socket";
          User = "storage";
        };
      };
    };
    sockets.u9fs = {
      description = "9P filesystem server socket";
      socketConfig = {
        Accept = "yes";
        ListenStream = "4500";
      };
      wantedBy = ["sockets.target"];
    };
  };
  time.timeZone = "America/Chicago";
  users = {
    groups.media = {};
    groups.storage = {};
    users = {
      media = {
        extraGroups = ["systemd-journal"];
        group = "media";
        isNormalUser = true;
        linger = true;
        packages = [self.packages.${pkgs.system}.neovim];
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
