{
  config,
  pkgs,
  self,
  ...
}: let
  mounts = [
    {
      what = "nas";
      where = "/var/www/n/media";
      type = "9p";
      options = "port=5640";
      after = ["network-online.target"];
      wants = ["network-online.target"];
    }
  ];
in {
  imports = [
    ./boot.nix
    self.nixosModules."9auth"
    self.nixosModules."9p-tools"
    self.nixosModules.fish
    self.nixosModules.locale
    self.nixosModules.nginx
    self.nixosModules.nix-config
  ];
  environment.systemPackages = [self.packages.${pkgs.stdenv.hostPlatform.system}.neovim];
  networking = rec {
    hostId = builtins.substring 0 8 (builtins.hashString "md5" hostName);
    hostName = "ingress";
    firewall = {
      allowedTCPPorts = [80 443];
      interfaces.${config.services.tailscale.interfaceName}.allowedTCPPorts = [22];
    };
    useDHCP = true;
  };
  services = {
    "9auth" = {
      enable = true;
      authorizedUsers = ["mpd"];
    };
    nginx-reverse-proxy = {
      enable = true;
      domain = "dargs.dev";
      filesRoot = "/var/www/n/media";
      publicRoot = "${self.packages.${pkgs.stdenv.hostPlatform.system}.www}";
      email = "matthewdargan57@gmail.com";
    };
    openssh = {
      enable = true;
      settings.PermitRootLogin = "no";
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
    services.authd = {
      description = "Session authentication daemon";
      after = ["network-online.target"];
      wants = ["network-online.target"];
      wantedBy = ["multi-user.target"];
      serviceConfig = {
        EnvironmentFile = "/var/lib/authd/secrets";
        ExecStart = "${self.packages.${pkgs.stdenv.hostPlatform.system}.authd}/bin/authd --auth-user=family";
        Group = "authd";
        NoNewPrivileges = true;
        PrivateTmp = true;
        ProtectHome = true;
        ProtectSystem = "strict";
        Restart = "always";
        RestartSec = "5s";
        RestrictSUIDSGID = true;
        RuntimeDirectory = "authd";
        RuntimeDirectoryMode = "0750";
        StateDirectory = "authd";
        StateDirectoryMode = "0700";
        TimeoutStartSec = "10s";
        TimeoutStopSec = "30s";
        User = "authd";
      };
    };
    tmpfiles.rules = [
      "d /var/www/n 0755 root root -"
    ];
  };
  system.stateVersion = "26.05";
  users = {
    groups.authd = {};
    users = {
      authd = {
        description = "Authentication daemon service user";
        isSystemUser = true;
        group = "authd";
      };
      mpd = {
        description = "Matthew Dargan";
        extraGroups = [
          "systemd-journal"
          "wheel"
        ];
        initialHashedPassword = "$y$j9T$hS7xTJo212Ak6724xjfxn1$AFWnScyq7dylmWw6QVtkZFEV4SSxI37lfaaKtkS9n5.";
        isNormalUser = true;
        openssh.authorizedKeys.keys = [
          "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAACAQDRhuefaFMT7Lzkoa2SQnRXl7UcLabk75Wxa2MVi4j37Z9gTr2u8oYXNWTTaGZ80IDICvsLczj+f1kEx2zXp8qY1VoOvXye1kRuQYwRhW+sW5QGz2KtXzNCK71bfChNc8xyvyqEg1c2JfKL/clLVMh47tq46TYt2x9RDFtE2vlQKf7tKmsgdyEJJYkxHhUbNe5WPlC/B9ForIdJO1CuwB7qfA8FB1l+Gw24j5SKkJgm+2Zn91GF0u6/eDlZSAFAOF61XAtmcZmpZdXBCNVkfEO+P0/7638BpEs4I6MSevKo97xwaDkXWpEXV3O7mZjbiwGdEtrRXLSPiOh5VnAykavvP6O8T37gO7FstdAIxDTWu2PerxPz5iX93A5bKGKD7TDPBHLjWxez1j+fhIgjKRicQXgUAt1RjOpZruL3/5DucG+GrjScW/jUtPV6mvuOsLguEIkXyfO0xgC6EzFKtKOmmnH6NjUspZ5eoPgdCQ7GLukksERY6bWm8N8pFy+SmUER1TC0V42ur1nMaTwlGkYJonOpe0BYQJRccukzxSSJNs+079Adqj1VCFY0QG1172i1KTmou34BhM+ZdKqrutsBWAoeO9aN2/7KnL9bPDIYHBDJEawspt+RBMb83dlm7rev0IlpsA8RxCkf+QNqYWjZzU3vX1Xn47r9136HYDaWAQ== openpgp:0xE9E3F438"
        ];
        shell = pkgs.fish;
      };
    };
  };
}
