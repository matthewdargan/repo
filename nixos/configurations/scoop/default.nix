{
  pkgs,
  self,
  ...
}: let
  mounts = [
    {
      what = "nas";
      where = "/home/mpd/n/nas";
      type = "9p";
      options = "port=4500";
      after = ["network-online.target"];
      wants = ["network-online.target"];
    }
    {
      what = "/home/mpd/n/nas/movies";
      where = "/home/mpd/n/movies";
      type = "none";
      options = "bind";
      requires = ["home-mpd-n-nas.mount"];
      after = ["home-mpd-n-nas.mount"];
    }
    {
      what = "/home/mpd/n/nas/shows";
      where = "/home/mpd/n/shows";
      type = "none";
      options = "bind";
      requires = ["home-mpd-n-nas.mount"];
      after = ["home-mpd-n-nas.mount"];
    }
    {
      what = "nas";
      where = "/mnt/nix-cache";
      type = "9p";
      options = "port=9564";
      after = ["network-online.target"];
      wants = ["network-online.target"];
    }
  ];
in {
  imports = [
    ./hardware.nix
    self.nixosModules."9p-tools"
    self.nixosModules.fish
    self.nixosModules.locale
    self.nixosModules.nix-cache-client
    self.nixosModules.nix-config
    self.nixosModules.settings
  ];
  boot.loader = {
    efi.canTouchEfiVariables = true;
    systemd-boot.enable = true;
  };
  networking = {
    firewall.checkReversePath = "loose";
    hostName = "scoop";
    networkmanager.enable = true;
  };
  programs.steam.enable = true;
  security.rtkit.enable = true;
  services = {
    desktopManager.plasma6.enable = true;
    displayManager.sddm = {
      enable = true;
      wayland.enable = true;
    };
    nix-cache-client.enable = true;
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
  systemd = {
    mounts = map (m: m // {wantedBy = [];}) mounts;
    automounts =
      map (m: {
        inherit (m) where;
        wantedBy = ["multi-user.target"];
        automountConfig.TimeoutIdleSec = "600";
      })
      mounts;
    services."9p-health-check" = {
      description = "Check 9P mount health and restart if stale";
      serviceConfig = {
        Type = "oneshot";
        User = "mpd";
      };
      script = ''
        if ${pkgs.systemd}/bin/systemctl is-active --quiet home-mpd-n-nas.mount; then
          if ! ${pkgs.coreutils}/bin/timeout 5 ${pkgs.coreutils}/bin/stat /home/mpd/n/nas >/dev/null 2>&1; then
            echo "9P mount at /home/mpd/n/nas is unresponsive, restarting..."
            ${pkgs.systemd}/bin/systemctl restart home-mpd-n-nas.mount
          fi
        fi
      '';
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
