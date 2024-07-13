{
  nixpkgs,
  sops-nix,
  ...
}: {
  config,
  pkgs,
  ...
}: {
  imports = [./hardware.nix sops-nix.nixosModules.sops];
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
        8096
        config.services.deluge.web.port
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
    deluge = {
      enable = true;
      dataDir = "/media/deluge";
      web.enable = true;
    };
    jellyfin.enable = true;
    openssh = {
      enable = true;
      settings.PermitRootLogin = "no";
    };
    postfix = {
      enable = true;
      config = {
        smtp_sasl_auth_enable = "yes";
        smtp_sasl_password_maps = "texthash:${config.sops.secrets."password".path}";
        smtp_use_tls = "yes";
        virtual_alias_maps = "inline:{ {root=matthewdargan57@gmail.com} }";
      };
      relayHost = "smtp.gmail.com";
      relayPort = 587;
    };
    tailscale.enable = true;
  };
  sops.secrets."password" = {
    owner = "postfix";
    sopsFile = "${../../..}/secrets/postfix.yaml";
  };
  system.stateVersion = "24.11";
  systemd = {
    services.check-btrfs-errors = {
      description = "Check BTRFS for errors";
      path = [pkgs.mailutils];
      requires = ["local-fs.target"];
      script = builtins.readFile ./check-btrfs-errors;
      serviceConfig = {
        Type = "oneshot";
      };
    };
    timers.check-btrfs-errors = {
      description = "check-btrfs-errors.service";
      timerConfig = {
        OnCalendar = "daily";
        Persistent = "true";
        Unit = "check-btrfs-errors.service";
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
