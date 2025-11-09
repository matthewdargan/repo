{
  pkgs,
  self,
  ...
}: {
  imports = [
    ./hardware.nix
    self.nixosModules."9p-tools"
    self.nixosModules.fish
    self.nixosModules.locale
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
