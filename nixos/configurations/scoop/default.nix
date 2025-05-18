{nixpkgs, ...}: {pkgs, ...}: {
  imports = [./hardware.nix];
  boot.loader = {
    efi.canTouchEfiVariables = true;
    systemd-boot.enable = true;
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
  networking = {
    hostName = "scoop";
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
  programs = {
    fish.enable = true;
    steam.enable = true;
  };
  security.rtkit.enable = true;
  services = {
    desktopManager.plasma6.enable = true;
    displayManager.sddm = {
      enable = true;
      wayland.enable = true;
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
    xserver = {
      enable = true;
      xkb = {
        layout = "us";
        variant = "";
      };
    };
  };
  system.stateVersion = "25.05";
  systemd.user.services."9pfuse-nas" = let
    mountPoint = "%h/n/nas";
    transport = "tcp";
    host = "nas";
    port = "4500";
    socket = "${transport}!${host}!${port}";
  in {
    enable = true;
    after = ["network-online.target"];
    description = "mounts 9p filesystem to directory";
    path = [
      "/run/wrappers" # needed for fusermount with setuid
    ];
    serviceConfig = {
      ExecStart = "${pkgs.plan9port}/plan9/bin/9pfuse '${socket}' '${mountPoint}'";
      ExecStop = "/run/wrappers/bin/fusermount -u '${mountPoint}'";
      RemainAfterExit = "yes";
      Type = "forking";
    };
    wants = ["network-online.target"];
    wantedBy = ["multi-user.target"];
  };
  time.timeZone = "America/Chicago";
  users.users.mpd = {
    description = "Matthew Dargan";
    extraGroups = ["input" "networkmanager" "systemd-journal" "wheel"];
    isNormalUser = true;
    shell = pkgs.fish;
  };
}
