{
  config,
  lib,
  pkgs,
  ...
}: let
  cfg = config.services.nix-client;
in {
  options.services.nix-client = {
    enable = lib.mkEnableOption "nix auto-update client";

    cacheMount = lib.mkOption {
      type = lib.types.str;
      default = "/n/nix";
      description = "Path where nix 9P filesystem is mounted";
    };

    hostname = lib.mkOption {
      type = lib.types.str;
      default = config.networking.hostName;
      description = "Hostname to use for looking up config (defaults to system hostname)";
    };

    interval = lib.mkOption {
      type = lib.types.str;
      default = "15min";
      description = "How often to check for updates (systemd time format)";
    };
  };

  config = lib.mkIf cfg.enable {
    systemd = {
      services.nix-update = {
        description = "Check and apply nix updates";
        path = [pkgs.nix pkgs.coreutils];

        script = ''
          set -euo pipefail
          STATE_DIR=/var/lib/nix-client
          STATE_FILE=$STATE_DIR/generation
          CACHE_FILE="${cfg.cacheMount}/configs/${cfg.hostname}"
          mkdir -p "$STATE_DIR"

          [ ! -f "$CACHE_FILE" ] && exit 0

          NEW_GEN=$(grep '^generation: ' "$CACHE_FILE" | cut -d' ' -f2)
          [ -z "$NEW_GEN" ] && exit 0

          CURRENT_GEN=$(cat "$STATE_FILE" 2>/dev/null || echo "0")
          [ "$NEW_GEN" = "$CURRENT_GEN" ] && exit 0

          STORE_PATH=$(grep '^path: ' "$CACHE_FILE" | cut -d' ' -f2)
          [ -z "$STORE_PATH" ] && exit 1

          nix-env --profile /nix/var/nix/profiles/system --set "$STORE_PATH"
          "$STORE_PATH/bin/switch-to-configuration" switch
          echo "$NEW_GEN" > "$STATE_FILE"
        '';

        serviceConfig = {
          Type = "oneshot";
          User = "root";
        };
      };

      timers.nix-update = {
        description = "Timer for nix updates";
        wantedBy = ["timers.target"];

        timerConfig = {
          OnBootSec = "5min";
          OnUnitActiveSec = cfg.interval;
          Persistent = true;
        };
      };

      tmpfiles.rules = ["d /var/lib/nix-client 0755 root root -"];
    };
  };
}
