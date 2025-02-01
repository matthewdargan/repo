{
  mooch,
  nixpkgs,
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
    supportedFilesystems = ["btrfs" "ext4" "vfat"];
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
      allowedTCPPorts = [22];
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
  services = {
    btrfs.autoScrub.enable = true;
    jellyfin.enable = true;
    openssh = {
      enable = true;
      settings.PermitRootLogin = "no";
    };
    tailscale.enable = true;
  };
  system.stateVersion = "24.11";
  systemd = {
    services.mooch = let
      configFile =
        pkgs.writeText "config.json"
        ''
          {
              "data_dir": "/media/rain",
              "feeds": [
                  {
                      "url": "https://example.com/rss?user=bob",
                      "pattern": "Popular Series - (\\d+) \\[1080p\\]\\[HEVC\\]",
                      "dst_dir": "/media/shows/Popular Series/Season 01"
                  },
                  {
                      "url": "https://another.org/feed?category=fantasy",
                      "pattern": "Ongoing Show - S03E(\\d+) \\[720p\\]",
                      "dst_dir": "/media/shows/Ongoing Show/Season 03"
                  }
              ]
          }
        '';
    in {
      description = "Download and organize torrents from RSS feeds";
      requires = ["local-fs.target"];
      serviceConfig = {
        ExecStart = "${mooch.packages.${pkgs.system}.mooch}/bin/mooch ${configFile}";
        Type = "oneshot";
        User = "jellyfin";
      };
    };
    timers.mooch = {
      description = "mooch.service";
      timerConfig = {
        OnCalendar = "daily";
        Persistent = "true";
        Unit = "mooch.service";
      };
      wantedBy = ["timers.target"];
    };
  };
  time.timeZone = "America/Chicago";
  users.users.mpd = {
    description = "Matthew Dargan";
    extraGroups = ["input" "networkmanager" "systemd-journal" "wheel"];
    isNormalUser = true;
    openssh.authorizedKeys.keys = [
      "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAILe/v2phdFJcaINc1bphWEM6vXDSlXY/e0B2zyb3ik1M matthewdargan57@gmail.com"
    ];
    shell = pkgs.bash;
  };
}
