{
  inputs,
  pkgs,
  self,
  ...
}: let
  mounts = [
    {
      what = "nas";
      where = "/home/mpd/n/media";
      type = "9p";
      options = "port=5640";
      after = ["network-online.target"];
      wants = ["network-online.target"];
    }
    {
      what = "nas";
      where = "/var/lib/nix-client/n/nix";
      type = "9p";
      options = "port=5641";
      after = ["network-online.target"];
      wants = ["network-online.target"];
    }
  ];
in {
  imports = [
    ./boot.nix
    inputs.jovian.nixosModules.jovian
    self.nixosModules."9p-health-check"
    self.nixosModules."9p-tools"
    self.nixosModules.fish
    self.nixosModules.locale
    self.nixosModules.nix-client
    self.nixosModules.nix-config
  ];
  jovian = {
    devices.steamdeck.enable = true;
    steam = {
      enable = true;
      desktopSession = "plasma";
      user = "mpd";
    };
  };
  networking = {
    hostName = "steamdeck";
    networkmanager.enable = true;
  };
  services = {
    "9p-health-check" = {
      enable = true;
      mounts = ["/home/mpd/n/media" "/var/lib/nix-client/n/nix"];
    };
    desktopManager.plasma6.enable = true;
    displayManager.sddm = {
      enable = true;
      wayland.enable = true;
    };
    nix-client.enable = true;
    openssh = {
      enable = true;
      settings.PermitRootLogin = "no";
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
  system.stateVersion = "26.05";
  users.users.mpd = {
    description = "Matthew Dargan";
    extraGroups = [
      "input"
      "networkmanager"
      "systemd-journal"
      "wheel"
    ];
    initialHashedPassword = "$y$j9T$GeE9dgxSIflY2YbqGPH27.$LvitWjXKM1u6jIIaXh0zDL/mbo9u9RMVQf37Omq5D8B";
    isNormalUser = true;
    openssh.authorizedKeys.keys = [
      "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAILe/v2phdFJcaINc1bphWEM6vXDSlXY/e0B2zyb3ik1M matthewdargan57@gmail.com"
    ];
    shell = pkgs.fish;
  };
}
