{
  pkgs,
  self,
  ...
}: {
  imports = [
    ./boot.nix
    ./network.nix
    ./services.nix
    self.nixosModules."9p-tools"
    self.nixosModules.fish
    self.nixosModules.locale
    self.nixosModules.settings
  ];
  environment.systemPackages = [
    pkgs.iproute2
    self.packages.${pkgs.stdenv.hostPlatform.system}.neovim
  ];
  system.stateVersion = "25.11";
  users.users.mpd = {
    description = "Matthew Dargan";
    extraGroups = [
      "input"
      "networkmanager"
      "systemd-journal"
      "wheel"
    ];
    initialHashedPassword = "$y$j9T$pjjMQ/FjAqPanlHgjkOHA/$/scKw5xPynqGjtdJkh9kh32upTEWKajelv1LtfqdemB";
    isNormalUser = true;
    openssh.authorizedKeys.keys = [
      "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAILe/v2phdFJcaINc1bphWEM6vXDSlXY/e0B2zyb3ik1M matthewdargan57@gmail.com"
    ];
    shell = pkgs.fish;
  };
}
