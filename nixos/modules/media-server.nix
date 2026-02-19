{
  config,
  lib,
  pkgs,
  self,
  ...
}: let
  cfg = config.services.media-server;
in {
  options.services.media-server = {
    enable = lib.mkEnableOption "media-server DASH streaming service";

    mediaRoot = lib.mkOption {
      type = lib.types.str;
      default = "/var/lib/media-server/media";
      description = "Root directory for media files";
      example = "/var/lib/media-server/n/media";
    };

    cacheRoot = lib.mkOption {
      type = lib.types.str;
      default = "/var/cache/media-server";
      description = "Root directory for transcoded DASH cache";
    };

    port = lib.mkOption {
      type = lib.types.port;
      default = 8080;
      description = "Port to listen on";
    };

    listenAddress = lib.mkOption {
      type = lib.types.str;
      default = "127.0.0.1";
      description = "Address to bind to (use 0.0.0.0 for all interfaces)";
      example = "100.64.0.1";
    };

    user = lib.mkOption {
      type = lib.types.str;
      default = "media-server";
      description = "User to run media-server as";
    };

    group = lib.mkOption {
      type = lib.types.str;
      default = "media-server";
      description = "Group to run media-server as";
    };

    mount9p = lib.mkOption {
      type = lib.types.nullOr (lib.types.submodule {
        options = {
          dial = lib.mkOption {
            type = lib.types.str;
            description = "9P dial string";
            example = "tcp!127.0.0.1!5640";
          };
          authId = lib.mkOption {
            type = lib.types.str;
            description = "Authentication ID for 9P";
            example = "nas";
          };
          dependsOn = lib.mkOption {
            type = lib.types.listOf lib.types.str;
            default = [];
            description = "Systemd services this mount depends on";
            example = ["media-serve.service"];
          };
        };
      });
      default = null;
      description = "Optional 9P mount configuration for media files";
    };

    openFirewall = lib.mkOption {
      type = lib.types.bool;
      default = false;
      description = "Open firewall port for media-server (only on tailscale interface)";
    };
  };

  config = lib.mkIf cfg.enable {
    environment.systemPackages = [
      self.packages.${pkgs.stdenv.hostPlatform.system}.media-server
    ];

    networking.firewall.interfaces = lib.mkIf cfg.openFirewall {
      ${config.services.tailscale.interfaceName}.allowedTCPPorts = [cfg.port];
    };

    services."9mount" = lib.mkIf (cfg.mount9p != null) {
      mounts = [
        {
          name = "media-server";
          inherit (cfg.mount9p) dial authId dependsOn;
          inherit (cfg) user;
          mountPoint = cfg.mediaRoot;
        }
      ];
    };

    systemd.services.media-server = {
      after = ["network.target"] ++ (lib.optionals (cfg.mount9p != null) ["9mount-media-server.service"]);
      description = "DASH media server with FFmpeg integration";
      serviceConfig = {
        ExecStart = "${self.packages.${pkgs.stdenv.hostPlatform.system}.media-server}/bin/media-server --media-root=${cfg.mediaRoot} --cache-root=${cfg.cacheRoot}";
        User = cfg.user;
        Group = cfg.group;
        Restart = "always";
        RestartSec = "5s";

        # Sandboxing
        CacheDirectory = "media-server";
        CacheDirectoryMode = "0750";
        NoNewPrivileges = true;
        PrivateTmp = true;
        ProtectSystem = "strict";
        ReadOnlyPaths = [cfg.mediaRoot];
        ReadWritePaths = [cfg.cacheRoot];
        RestrictSUIDSGID = true;

        # Resource limits
        MemoryMax = "4G";
        TasksMax = 128;
      };
      wants = lib.optionals (cfg.mount9p != null) ["9mount-media-server.service"];
      wantedBy = ["multi-user.target"];
    };

    systemd.tmpfiles.rules = [
      "d ${cfg.cacheRoot} 0750 ${cfg.user} ${cfg.group} -"
      "d ${lib.dirOf cfg.mediaRoot} 0755 ${cfg.user} ${cfg.group} -"
    ];

    users.groups.${cfg.group} = lib.mkIf (cfg.group == "media-server") {};
    users.users.${cfg.user} = lib.mkIf (cfg.user == "media-server") {
      inherit (cfg) group;
      isSystemUser = true;
    };
  };
}
