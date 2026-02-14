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
    self.nixosModules.fish
    self.nixosModules.locale
    self.nixosModules.nix-config
    self.nixosModules.yubikey
  ];
  boot.loader = {
    efi.canTouchEfiVariables = true;
    systemd-boot.enable = true;
  };
  environment.systemPackages = [
    self.packages.${pkgs.stdenv.hostPlatform.system}."9p"
  ];
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
      authorizedUsers = [user];
    };
    "9mount" = {
      enable = true;
      mounts = [
        {
          name = "media";
          dial = "tcp!nas!5640";
          mountPoint = "/home/${user}/n/media";
          authId = "nas";
          dependsOn = ["tailscaled.service"];
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
