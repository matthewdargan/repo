{nixpkgs, ...}: {pkgs, ...}: {
  imports = [./hardware.nix];
  boot.loader = {
    efi.canTouchEfiVariables = true;
    systemd-boot.enable = true;
  };
  hardware.pulseaudio.enable = false;
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
    };
  };
  nixpkgs.config.allowUnfree = true;
  programs.steam.enable = true;
  security.rtkit.enable = true;
  services = {
    displayManager.sddm.enable = true;
    ollama = {
      enable = true;
      acceleration = "rocm";
      rocmOverrideGfx = "11.0.0";
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
      desktopManager.plasma5.enable = true;
      xkb = {
        layout = "us";
        variant = "";
      };
    };
  };
  sound.enable = true;
  system.stateVersion = "24.11";
  time.timeZone = "America/Chicago";
  users.users.mpd = {
    description = "Matthew Dargan";
    extraGroups = ["input" "networkmanager" "systemd-journal" "wheel"];
    isNormalUser = true;
    shell = pkgs.bash;
  };
}
