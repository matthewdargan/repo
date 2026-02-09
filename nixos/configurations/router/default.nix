{
  pkgs,
  self,
  ...
}: {
  imports = [
    ./boot.nix
    ./network.nix
    ./services.nix
    self.nixosModules."9auth"
    self.nixosModules."9p-tools"
    self.nixosModules.fish
    self.nixosModules.locale
    self.nixosModules.nix-config
  ];
  environment.systemPackages = [
    pkgs.iproute2
    self.packages.${pkgs.stdenv.hostPlatform.system}.neovim
  ];
  services."9auth" = {
    enable = true;
    authorizedUsers = ["mpd"];
  };
  system.stateVersion = "25.11";
  users.users.mpd = {
    description = "Matthew Dargan";
    extraGroups = [
      "input"
      "systemd-journal"
      "wheel"
    ];
    initialHashedPassword = "$y$j9T$pjjMQ/FjAqPanlHgjkOHA/$/scKw5xPynqGjtdJkh9kh32upTEWKajelv1LtfqdemB";
    isNormalUser = true;
    openssh.authorizedKeys.keys = [
      "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAACAQDRhuefaFMT7Lzkoa2SQnRXl7UcLabk75Wxa2MVi4j37Z9gTr2u8oYXNWTTaGZ80IDICvsLczj+f1kEx2zXp8qY1VoOvXye1kRuQYwRhW+sW5QGz2KtXzNCK71bfChNc8xyvyqEg1c2JfKL/clLVMh47tq46TYt2x9RDFtE2vlQKf7tKmsgdyEJJYkxHhUbNe5WPlC/B9ForIdJO1CuwB7qfA8FB1l+Gw24j5SKkJgm+2Zn91GF0u6/eDlZSAFAOF61XAtmcZmpZdXBCNVkfEO+P0/7638BpEs4I6MSevKo97xwaDkXWpEXV3O7mZjbiwGdEtrRXLSPiOh5VnAykavvP6O8T37gO7FstdAIxDTWu2PerxPz5iX93A5bKGKD7TDPBHLjWxez1j+fhIgjKRicQXgUAt1RjOpZruL3/5DucG+GrjScW/jUtPV6mvuOsLguEIkXyfO0xgC6EzFKtKOmmnH6NjUspZ5eoPgdCQ7GLukksERY6bWm8N8pFy+SmUER1TC0V42ur1nMaTwlGkYJonOpe0BYQJRccukzxSSJNs+079Adqj1VCFY0QG1172i1KTmou34BhM+ZdKqrutsBWAoeO9aN2/7KnL9bPDIYHBDJEawspt+RBMb83dlm7rev0IlpsA8RxCkf+QNqYWjZzU3vX1Xn47r9136HYDaWAQ== openpgp:0xE9E3F438"
    ];
    shell = pkgs.fish;
  };
}
