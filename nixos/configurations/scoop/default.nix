{
  inputs,
  pkgs,
  self,
  ...
}: {
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
    nixPath = ["nixpkgs=${inputs.nixpkgs}"];
    registry.nixpkgs.flake = inputs.nixpkgs;
    settings = {
      auto-optimise-store = true;
      experimental-features = [
        "nix-command"
        "flakes"
      ];
      trusted-users = ["@wheel"];
    };
  };
  nixpkgs.config.allowUnfree = true;
  programs = {
    fish.enable = true;
    steam.enable = true;
  };
  security = {
    rtkit.enable = true;
    wrappers = {
      "9bind" = {
        owner = "root";
        group = "root";
        permissions = "u+rx,g+x,o+x";
        setuid = true;
        source = "${self.packages.${pkgs.system}."9bind"}/bin/9bind";
      };
      "9mount" = {
        owner = "root";
        group = "root";
        permissions = "u+rx,g+x,o+x";
        setuid = true;
        source = "${self.packages.${pkgs.system}."9mount"}/bin/9mount";
      };
      "9umount" = {
        owner = "root";
        group = "root";
        permissions = "u+rx,g+x,o+x";
        setuid = true;
        source = "${self.packages.${pkgs.system}."9umount"}/bin/9umount";
      };
    };
  };
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
  systemd.services."nas-mount" = {
    after = [
      "network-online.target"
    ];
    description = "mount nas";
    serviceConfig = {
      ExecStart = [
        ''/bin/sh -c "if ${pkgs.util-linux}/bin/mountpoint -q /home/mpd/n/nas; then /run/wrappers/bin/9umount /home/mpd/n/nas; fi"''
        ''/bin/sh -c "if ${pkgs.util-linux}/bin/mountpoint -q /home/mpd/n/movies; then /run/wrappers/bin/9umount /home/mpd/n/movies; fi"''
        ''/bin/sh -c "if ${pkgs.util-linux}/bin/mountpoint -q /home/mpd/n/shows; then /run/wrappers/bin/9umount /home/mpd/n/shows; fi"''
        "/run/wrappers/bin/9mount 'tcp!nas!4500' /home/mpd/n/nas"
        "/run/wrappers/bin/9bind /home/mpd/n/nas/movies /home/mpd/n/movies"
        "/run/wrappers/bin/9bind /home/mpd/n/nas/shows /home/mpd/n/shows"
      ];
      ExecStartPre = [
        "${pkgs.coreutils}/bin/mkdir -p /home/mpd/n/nas /home/mpd/n/movies /home/mpd/n/shows"
      ];
      RemainAfterExit = true;
      Restart = "on-failure";
      RestartSec = "5s";
      Type = "oneshot";
    };
    wantedBy = ["multi-user.target"];
    wants = [
      "network-online.target"
    ];
  };
  system.stateVersion = "25.05";
  time.timeZone = "America/Chicago";
  users.users.mpd = {
    description = "Matthew Dargan";
    extraGroups = [
      "input"
      "networkmanager"
      "systemd-journal"
      "wheel"
    ];
    isNormalUser = true;
    shell = pkgs.fish;
  };
}
