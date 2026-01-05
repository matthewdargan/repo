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
      options = "port=5640,msize=1048576";
      after = ["network-online.target"];
      wants = ["network-online.target"];
    }
    {
      what = "nas";
      where = "/var/lib/nix-client/n/nix";
      type = "9p";
      options = "port=5641,msize=1048576";
      after = ["network-online.target"];
      wants = ["network-online.target"];
    }
  ];
in {
  imports = [
    ./boot.nix
    self.nixosModules."9p-health-check"
    self.nixosModules."9p-tools"
    self.nixosModules.fish
    self.nixosModules.locale
    self.nixosModules.nginx
    self.nixosModules.nix-client
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
    "9p-health-check" = {
      enable = true;
      mounts = ["/var/www/n/media" "/var/lib/nix-client/n/nix"];
    };
    nginx-reverse-proxy = {
      enable = true;
      domain = "dargs.dev";
      filesRoot = "/var/www/n/media";
      publicRoot = "${self.packages.${pkgs.stdenv.hostPlatform.system}.www}";
      email = "matthewdargan57@gmail.com";
    };
    nix-client.enable = true;
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
          "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAILe/v2phdFJcaINc1bphWEM6vXDSlXY/e0B2zyb3ik1M matthewdargan57@gmail.com"
        ];
        shell = pkgs.fish;
      };
    };
  };
}
