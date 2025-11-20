{
  config,
  pkgs,
  self,
  ...
}: let
  mounts = [
    {
      what = "127.0.0.1";
      where = "/home/media/n/nas";
      type = "9p";
      options = "port=4500";
      requires = ["9pfs.service"];
      after = ["9pfs.service" "network-online.target"];
      wants = ["network-online.target"];
    }
    {
      what = "/home/media/n/nas/movies";
      where = "/home/media/n/movies";
      type = "none";
      options = "bind";
      requires = ["home-media-n-nas.mount"];
      after = ["home-media-n-nas.mount"];
    }
    {
      what = "/home/media/n/nas/shows";
      where = "/home/media/n/shows";
      type = "none";
      options = "bind";
      requires = ["home-media-n-nas.mount"];
      after = ["home-media-n-nas.mount"];
    }
  ];
in {
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
    mounts = map (m: m // {wantedBy = [];}) mounts;
    automounts =
      map (m: {
        inherit (m) where;
        wantedBy = ["multi-user.target"];
        automountConfig.TimeoutIdleSec = "600";
      })
      mounts;
    services = {
      jellyfin = {
        after = ["home-media-n-movies.automount" "home-media-n-shows.automount"];
        wants = ["home-media-n-movies.automount" "home-media-n-shows.automount"];
      };
      "9pfs" = {
        after = ["network.target"];
        description = "9P filesystem server";
        serviceConfig = {
          ExecStart = "${self.packages.${pkgs.stdenv.hostPlatform.system}."9pfs"}/bin/9pfs --root=/media tcp!*!4500";
          Restart = "always";
          RestartSec = "5s";
          StandardError = "journal";
          StandardOutput = "journal";
          User = "storage";
        };
        wantedBy = ["multi-user.target"];
      };
      "9p-health-check" = {
        description = "Check 9P mount health and restart if stale";
        serviceConfig = {
          Type = "oneshot";
          User = "media";
        };
        script = ''
          if ${pkgs.systemd}/bin/systemctl is-active --quiet home-media-n-nas.mount; then
            if ! ${pkgs.coreutils}/bin/timeout 5 ${pkgs.coreutils}/bin/stat /home/media/n/nas >/dev/null 2>&1; then
              echo "9P mount at /home/media/n/nas is unresponsive, restarting..."
              ${pkgs.systemd}/bin/systemctl restart home-media-n-nas.mount
            fi
          fi
        '';
      };
    };
    timers."9p-health-check" = {
      wantedBy = ["timers.target"];
      timerConfig = {
        OnBootSec = "5min";
        OnUnitActiveSec = "5min";
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
