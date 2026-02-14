{
  config,
  lib,
  pkgs,
  self,
  ...
}: let
  cfg = config.services."9auth";
in {
  options.services."9auth" = {
    enable = lib.mkEnableOption "9auth authentication daemon";
    authorizedUsers = lib.mkOption {
      type = lib.types.listOf lib.types.str;
      default = [];
      description = "List of users authorized to use 9auth";
      example = ["mpd"];
    };
  };

  config = lib.mkIf cfg.enable {
    environment.systemPackages = [
      self.packages.${pkgs.stdenv.hostPlatform.system}."9auth"
    ];
    systemd = {
      services."9auth" = {
        after = ["network.target"];
        description = "9auth authentication daemon";
        serviceConfig = {
          ExecStart = "${self.packages.${pkgs.stdenv.hostPlatform.system}."9auth"}/bin/9auth";
          Group = "9auth";
          NoNewPrivileges = true;
          PrivateTmp = true;
          ProtectHome = true;
          ProtectSystem = "strict";
          Restart = "always";
          RestartSec = "5s";
          RestrictSUIDSGID = true;
          RuntimeDirectory = "9auth";
          RuntimeDirectoryMode = "0750";
          StateDirectory = "9auth";
          StateDirectoryMode = "0700";
          TimeoutStartSec = "10s";
          TimeoutStopSec = "30s";
          User = "9auth";
        };
        wantedBy = ["multi-user.target"];
      };
      tmpfiles.rules = [
        "f /var/lib/9auth/keys 0600 9auth 9auth -"
      ];
    };
    users = {
      groups."9auth" = {};
      users =
        {
          "9auth" = {
            group = "9auth";
            isSystemUser = true;
          };
        }
        // lib.listToAttrs (map (user: {
            name = user;
            value.extraGroups = ["9auth"];
          })
          cfg.authorizedUsers);
    };
  };
}
