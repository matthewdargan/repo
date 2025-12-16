{
  pkgs,
  self,
  ...
}: let
  mounts = [
    {
      what = "nas";
      where = "/n/media";
      type = "9p";
      options = "port=5640";
      after = ["network-online.target"];
      wants = ["network-online.target"];
    }
    {
      what = "nas";
      where = "/n/nix";
      type = "9p";
      options = "port=5641";
      after = ["network-online.target"];
      wants = ["network-online.target"];
    }
  ];
in {
  imports = [
    ./hardware.nix
    self.nixosModules."9p-health-check"
    self.nixosModules."9p-tools"
    self.nixosModules.fish
    self.nixosModules.locale
    self.nixosModules.nix-client
    self.nixosModules.nix-config
  ];
  boot.loader = {
    efi.canTouchEfiVariables = true;
    systemd-boot.enable = true;
  };
  networking = {
    hostName = "scoop";
    firewall.checkReversePath = "loose";
    networkmanager.enable = true;
  };
  programs.steam.enable = true;
  security.rtkit.enable = true;
  services = {
    "9p-health-check" = {
      enable = true;
      mounts = ["n-media" "n-nix"];
    };
    desktopManager.plasma6.enable = true;
    displayManager.sddm = {
      enable = true;
      wayland.enable = true;
    };
    nix-client.enable = true;
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
