{
  config,
  pkgs,
  self,
  ...
}: let
  mounts = [
    {
      what = "nas";
      where = "/var/lib/httpproxy/n/media";
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
    self.nixosModules."9p-health-check"
    self.nixosModules."9p-tools"
    self.nixosModules.fish
    self.nixosModules.locale
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
      mounts = ["/var/lib/httpproxy/n/media" "/var/lib/nix-client/n/nix"];
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
    services.httpproxy = {
      description = "HTTP reverse proxy with TLS termination";
      after = ["network-online.target"];
      wants = ["network-online.target"];
      wantedBy = ["multi-user.target"];
      serviceConfig = {
        AmbientCapabilities = ["CAP_NET_BIND_SERVICE"];
        CapabilityBoundingSet = ["CAP_NET_BIND_SERVICE"];
        EnvironmentFile = "/var/lib/httpproxy/secrets";
        ExecStart = builtins.concatStringsSep " " [
          "${self.packages.${pkgs.stdenv.hostPlatform.system}.httpproxy}/bin/httpproxy"
          "--acme-domain=dargs.dev"
          "--file-root=${self.packages.${pkgs.stdenv.hostPlatform.system}.www}"
          "--private-root=/var/lib/httpproxy/n/media/private"
          "--auth-user=family"
          "--auth-password=\${AUTH_PASSWORD}"
        ];
        Group = "httpproxy";
        NoNewPrivileges = true;
        PrivateTmp = true;
        ProtectHome = true;
        ProtectSystem = "strict";
        ReadOnlyPaths = ["/var/lib/httpproxy/n/media"];
        Restart = "always";
        RestartSec = "5s";
        RestrictSUIDSGID = true;
        RuntimeDirectory = "httpproxy";
        RuntimeDirectoryMode = "0750";
        StateDirectory = "httpproxy";
        StateDirectoryMode = "0700";
        TimeoutStartSec = "10s";
        TimeoutStopSec = "30s";
        User = "httpproxy";
      };
    };
    tmpfiles.rules = [
      "d /var/lib/httpproxy/n 0755 httpproxy httpproxy -"
    ];
  };
  system.stateVersion = "26.05";
  users = {
    groups.httpproxy = {};
    users = {
      httpproxy = {
        description = "HTTP reverse proxy service user";
        isSystemUser = true;
        group = "httpproxy";
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
