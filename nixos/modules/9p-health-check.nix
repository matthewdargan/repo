{
  config,
  lib,
  pkgs,
  ...
}: let
  cfg = config.services."9p-health-check";
in {
  options.services."9p-health-check" = {
    enable = lib.mkEnableOption "9P mount health monitoring";

    mounts = lib.mkOption {
      type = lib.types.listOf lib.types.str;
      default = [];
      description = "List of mount paths to monitor (e.g., /n/media, /var/lib/nix-client/n/nix)";
      example = ["/n/media" "/var/lib/nix-client/n/nix"];
    };

    interval = lib.mkOption {
      type = lib.types.str;
      default = "5min";
      description = "How often to check mount health (systemd time format)";
    };
  };

  config = lib.mkIf cfg.enable {
    systemd = {
      services."9p-health-check" = {
        description = "Check 9P mount health and restart if stale";
        path = [pkgs.systemd pkgs.coreutils];
        serviceConfig = {
          Type = "oneshot";
          User = "root";
        };
        script = ''
          ${lib.concatMapStringsSep "\n" (mountPath: ''
              unit_name=$(systemd-escape --path "${mountPath}")
              if systemctl is-active --quiet "$unit_name.mount"; then
                if ! timeout 5 stat "${mountPath}" >/dev/null 2>&1; then
                  echo "9P mount at ${mountPath} is unresponsive, restarting..."
                  systemctl restart "$unit_name.mount"
                fi
              fi
            '')
            cfg.mounts}
        '';
      };

      timers."9p-health-check" = {
        description = "Timer for 9P mount health checks";
        wantedBy = ["timers.target"];
        timerConfig = {
          OnBootSec = cfg.interval;
          OnUnitActiveSec = cfg.interval;
        };
      };
    };
  };
}
