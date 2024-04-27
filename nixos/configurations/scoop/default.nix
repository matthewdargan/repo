{nixpkgs, ...}: {pkgs, ...}: {
  imports = [./hardware.nix];
  boot.loader = {
    efi.canTouchEfiVariables = true;
    systemd-boot.enable = true;
  };
  environment.systemPackages = [
    pkgs.pkgsi686Linux.gperftools # https://github.com/NixOS/nixpkgs/issues/271483#issuecomment-1838055011
  ];
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
    nixPath = ["nixpkgs=${nixpkgs}"];
    registry.nixpkgs.flake = nixpkgs;
    settings.experimental-features = "nix-command flakes";
  };
  nixpkgs.config.allowUnfree = true;
  programs.steam.enable = true;
  security.rtkit.enable = true;
  services = {
    displayManager.sddm.enable = true;
    pipewire = {
      enable = true;
      alsa = {
        enable = true;
        support32Bit = true;
      };
      pulse.enable = true;
    };
    postgresql = {
      enable = true;
      authentication = "local all all peer map=superuser_map";
      ensureDatabases = ["ebay"];
      identMap = ''
        superuser_map mpd postgres
        superuser_map postgres postgres
        superuser_map /^(.*)$ \1
      '';
      package = pkgs.postgresql_16;
    };
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
  system.stateVersion = "23.11";
  time.timeZone = "America/Chicago";
  users.users.mpd = {
    description = "Matthew Dargan";
    extraGroups = ["input" "networkmanager" "systemd-journal" "wheel"];
    isNormalUser = true;
    shell = pkgs.bash;
  };
}
