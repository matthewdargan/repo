{
  config,
  lib,
  pkgs,
  self,
  ...
}: let
  cfg = config.services."9mount";
  mountOpts = {
    options = {
      name = lib.mkOption {
        type = lib.types.str;
        description = "Name of the mount (used for service naming)";
      };

      dial = lib.mkOption {
        type = lib.types.str;
        description = "Dial string for the 9p server (e.g., tcp!nas!5640)";
      };
      mountPoint = lib.mkOption {
        type = lib.types.path;
        description = "Local mount point directory";
      };

      useAuth = lib.mkOption {
        type = lib.types.bool;
        default = true;
        description = "Whether to use authentication via 9auth";
      };
      aname = lib.mkOption {
        type = lib.types.str;
        default = "/";
        description = "Remote attach name (aname)";
      };
      authServer = lib.mkOption {
        type = lib.types.str;
        default = "unix!/run/9auth/socket";
        description = "Address of the 9auth daemon";
      };

      dependsOn = lib.mkOption {
        type = lib.types.listOf lib.types.str;
        default = [];
        description = "Systemd services that must be running before mounting";
      };
      user = lib.mkOption {
        type = lib.types.str;
        description = "User to run the mount as";
      };
    };
  };
in {
  options.services."9mount" = {
    enable = lib.mkEnableOption "9P FUSE mount service";
    mounts = lib.mkOption {
      type = lib.types.listOf (lib.types.submodule mountOpts);
      default = [];
      description = "List of FUSE 9P mounts to manage";
    };
  };

  config = lib.mkIf cfg.enable {
    boot.kernelModules = ["fuse"];
    systemd.services = lib.listToAttrs (
      map (m: let
        args = lib.concatStringsSep " " (
          lib.filter (x: x != "") [
            (lib.optionalString m.useAuth "--auth")
            (lib.optionalString (m.authServer != "unix!/run/9auth/socket")
              "--auth-server=${lib.escapeShellArg m.authServer}")
            "--aname=${lib.escapeShellArg m.aname}"
            (lib.escapeShellArg m.dial)
            (lib.escapeShellArg m.mountPoint)
          ]
        );
      in {
        name = "9mount-${m.name}";
        value = {
          after = ["network-online.target"] ++ m.dependsOn;
          description = "9P mount for ${m.name}";
          requires = m.dependsOn;
          wants = ["network-online.target"];
          wantedBy = ["multi-user.target"];
          serviceConfig = {
            ExecStart = pkgs.writeShellScript "9mount-${m.name}" ''
              set -euo pipefail
              if ${pkgs.util-linux}/bin/mountpoint -q ${lib.escapeShellArg m.mountPoint}; then
                ${pkgs.fuse3}/bin/fusermount3 -u ${lib.escapeShellArg m.mountPoint} 2>/dev/null || \
                  ${pkgs.util-linux}/bin/umount -l ${lib.escapeShellArg m.mountPoint} || true
              fi
              ${pkgs.coreutils}/bin/mkdir -p ${lib.escapeShellArg m.mountPoint}
              exec ${self.packages.${pkgs.stdenv.hostPlatform.system}."9mount"}/bin/9mount ${args}
            '';
            ExecStop = "${pkgs.fuse3}/bin/fusermount3 -u ${lib.escapeShellArg m.mountPoint}";
            KillMode = "control-group";
            Restart = "on-failure";
            RestartSec = "5s";
            TimeoutStopSec = "10s";
            Type = "forking";
            User = m.user;
          };
        };
      })
      cfg.mounts
    );
  };
}
