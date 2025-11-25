{
  config,
  lib,
  pkgs,
  ...
}: let
  cfg = config.services.nix-cache-client;
in {
  options.services.nix-cache-client = {
    enable = lib.mkEnableOption "nix-cache auto-update client";

    cacheMount = lib.mkOption {
      type = lib.types.str;
      default = "/mnt/nix-cache";
      description = "Path where nix-cache 9P filesystem is mounted";
    };

    hostname = lib.mkOption {
      type = lib.types.str;
      default = config.networking.hostName;
      description = "Hostname to use for looking up config in nix-cache (defaults to system hostname)";
    };

    interval = lib.mkOption {
      type = lib.types.str;
      default = "15min";
      description = "How often to check for updates (systemd time format)";
    };
  };

  config = lib.mkIf cfg.enable {
    systemd = {
      services.nix-cache-update = {
        description = "Check and apply nix-cache updates";
        path = [pkgs.nix pkgs.coreutils];

        script = ''
          set -euo pipefail
          STATE_DIR=/var/lib/nix-cache-client
          STATE_FILE=$STATE_DIR/generation
          CACHE="${cfg.cacheMount}/configs/${cfg.hostname}"
          mkdir -p "$STATE_DIR"

          # Check if cache is accessible
          if [ ! -d "$CACHE" ]; then
            echo "nix-cache-update: cache not accessible at $CACHE"
            exit 0
          fi

          NEW_GEN=$(cat "$CACHE/generation" 2>/dev/null || echo "")
          [ -z "$NEW_GEN" ] && exit 0

          CURRENT_GEN=$(cat "$STATE_FILE" 2>/dev/null || echo "0")
          [ "$NEW_GEN" = "$CURRENT_GEN" ] && exit 0

          STORE_PATH=$(cat "$CACHE/path" 2>/dev/null || echo "")
          [ -z "$STORE_PATH" ] && exit 1

          echo "nix-cache-update: $CURRENT_GEN -> $NEW_GEN: $STORE_PATH"

          # Fetch from cache and activate
          nix-env --profile /nix/var/nix/profiles/system \
            --set "$STORE_PATH" \
            --option substituters "file://${cfg.cacheMount}" \
            --option require-sigs false

          "$STORE_PATH/bin/switch-to-configuration" switch

          echo "$NEW_GEN" > "$STATE_FILE"
        '';

        serviceConfig = {
          Type = "oneshot";
          User = "root";
        };
      };

      timers.nix-cache-update = {
        description = "Timer for nix-cache updates";
        wantedBy = ["timers.target"];

        timerConfig = {
          OnBootSec = "5min";
          OnUnitActiveSec = cfg.interval;
          Persistent = true;
        };
      };

      tmpfiles.rules = ["d /var/lib/nix-cache-client 0755 root root -"];
    };
  };
}
