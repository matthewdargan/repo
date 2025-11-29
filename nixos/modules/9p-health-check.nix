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
      description = "List of systemd mount unit names to monitor (e.g., n-media, n-nix)";
      example = ["n-media" "n-nix"];
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
        serviceConfig = {
          Type = "oneshot";
          User = "root";
        };
        script = ''
          for mount in ${lib.concatStringsSep " " cfg.mounts}; do
            if ${pkgs.systemd}/bin/systemctl is-active --quiet $mount.mount; then
              path="/''${mount//-//}"
              if ! ${pkgs.coreutils}/bin/timeout 5 ${pkgs.coreutils}/bin/stat "$path" >/dev/null 2>&1; then
                echo "9P mount at $path is unresponsive, restarting..."
                ${pkgs.systemd}/bin/systemctl restart $mount.mount
              fi
            fi
          done
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
