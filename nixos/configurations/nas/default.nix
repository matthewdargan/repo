{
  media-server,
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
      allowedTCPPorts = [22 4500];
      checkReversePath = "loose";
      interfaces.${config.services.tailscale.interfaceName}.allowedTCPPorts = [
        7246
        8080
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
  services = {
    btrfs.autoScrub.enable = true;
    jellyfin = {
      enable = true;
      openFirewall = true;
    };
    openssh = {
      enable = true;
      settings.PermitRootLogin = "no";
    };
    tailscale.enable = true;
  };
  system.stateVersion = "25.05";
  systemd = let
    mountDir = "/home/mpd/9drive";
    port = "4500";
    user = "mpd";
  in {
    services = {
      mediasrv = {
        enable = true;
        after = ["network.target"];
        description = "media server";
        wantedBy = ["multi-user.target"];
        serviceConfig = {
          ExecStart = "${media-server.packages.${pkgs.system}.mediasrv}/bin/mediasrv -i /media/shows -o /var/lib/mediasrv -p 8080";
          Restart = "on-failure";
          StateDirectory = "mediasrv";
          Type = "simple";
          User = "${user}";
        };
      };
      "u9fs@" = {
        after = ["network.target"];
        description = "serves directory as a 9p root filesystem";
        serviceConfig = {
          ExecStart = "${pkgs.u9fs}/bin/u9fs -D -a none -u ${user} -d ${mountDir}";
          StandardInput = "socket";
          StandardError = "journal";
          User = "${user}";
        };
      };
    };
    sockets.u9fs = {
      description = "9P filesystem server socket";
      socketConfig = {
        Accept = "yes";
        ListenStream = port;
      };
      wantedBy = ["sockets.target"];
    };
  };
  time.timeZone = "America/Chicago";
  users.users.mpd = {
    description = "Matthew Dargan";
    extraGroups = ["input" "jellyfin" "networkmanager" "systemd-journal" "wheel"];
    isNormalUser = true;
    openssh.authorizedKeys.keys = [
      "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAILe/v2phdFJcaINc1bphWEM6vXDSlXY/e0B2zyb3ik1M matthewdargan57@gmail.com"
    ];
    shell = pkgs.fish;
  };
}
