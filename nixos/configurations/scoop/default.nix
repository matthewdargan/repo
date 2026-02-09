{
  pkgs,
  self,
  ...
}: let
  user = "mpd";
in {
  imports = [
    ./hardware.nix
    self.nixosModules."9auth"
    self.nixosModules."9mount"
    self.nixosModules."9p-tools"
    self.nixosModules.fish
    self.nixosModules.locale
    self.nixosModules.nix-config
    self.nixosModules.yubikey
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
    "9auth" = {
      enable = true;
      authorizedUsers = ["mpd"];
    };
    "9mount" = {
      enable = true;
      mounts = [
        {
          name = "media";
          dial = "tcp!nas!5640";
          mountPoint = "/home/${user}/n/media";
          useAuth = false;
          inherit user;
        }
      ];
    };
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
  users.users.${user} = {
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
  yubikey = {
    enable = true;
    inherit user;
  };
}
