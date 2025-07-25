{
  nixpkgs,
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
    mountDir = "/media";
    port = "4500";
    user = "mpd";
  in {
    services."u9fs@" = {
      after = ["network.target"];
      description = "9P filesystem server";
      serviceConfig = {
        # TODO: replace user with a different user that manages /media i.e. least access
        ExecStart = "${u9fs.packages.${pkgs.system}.u9fs}/bin/u9fs -D -a none -u ${user} -d ${mountDir}";
        StandardInput = "socket";
        StandardError = "journal";
        User = "${user}";
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
    user.services."nas-mount" = {
      after = ["network.target"];
      description = "mount nas";
      serviceConfig = {
        ExecStart = [
          "/run/wrappers/bin/9mount 'tcp!nas!4500' /home/mpd/n/nas"
          "/run/wrappers/bin/9bind /home/mpd/n/nas/movies /home/mpd/n/movies"
          "/run/wrappers/bin/9bind /home/mpd/n/nas/shows /home/mpd/n/shows"
        ];
        ExecStartPre = [
          "${pkgs.coreutils}/bin/mkdir -p /home/mpd/n/nas /home/mpd/n/movies /home/mpd/n/shows"
        ];
        ExecStop = ["/run/wrappers/bin/9umount /home/mpd/n/nas /home/mpd/n/movies /home/mpd/n/shows"];
        RemainAfterExit = true;
        Type = "oneshot";
      };
      wantedBy = ["multi-user.target"];
    };
  };
  time.timeZone = "America/Chicago";
  users.users.mpd = {
    description = "Matthew Dargan";
    extraGroups = [
      "input"
      "jellyfin"
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
}
