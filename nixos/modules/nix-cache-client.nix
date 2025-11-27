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

    substituters = lib.mkOption {
      type = lib.types.listOf lib.types.str;
      default = ["file://${cfg.cacheMount}"];
      description = "List of substituters to use when building";
    };
  };

  config = lib.mkIf cfg.enable {
    systemd = {
      services.nix-cache-update = {
        description = "Check and apply nix-cache updates";
        path = [pkgs.nix pkgs.coreutils pkgs.gawk];

        script = ''
          set -euo pipefail

          CACHE="${cfg.cacheMount}/configs/${cfg.hostname}"
          STATE_DIR="/var/lib/nix-cache-client"
          STATE_FILE="$STATE_DIR/generation"

          mkdir -p "$STATE_DIR"

          if [ ! -d "${cfg.cacheMount}/configs/${cfg.hostname}" ]; then
            echo "nix-cache-update: config directory not found at $CACHE"
            exit 0
          fi

          if [ ! -f "$CACHE/generation" ]; then
            echo "nix-cache-update: no generation file found at $CACHE/generation"
            exit 0
          fi

          NEW_GEN=$(cat "$CACHE/generation" 2>/dev/null || echo "")
          if [ -z "$NEW_GEN" ]; then
            echo "nix-cache-update: empty generation file"
            exit 0
          fi

          CURRENT_GEN=$(cat "$STATE_FILE" 2>/dev/null || echo "0")

          if [ "$NEW_GEN" = "$CURRENT_GEN" ]; then
            echo "nix-cache-update: already on generation $CURRENT_GEN"
            exit 0
          fi

          STORE_PATH=$(cat "$CACHE/path" 2>/dev/null || echo "")
          if [ -z "$STORE_PATH" ]; then
            echo "nix-cache-update: empty path file"
            exit 1
          fi

          echo "nix-cache-update: updating from generation $CURRENT_GEN to $NEW_GEN"
          echo "nix-cache-update: store path: $STORE_PATH"

          nixos-rebuild switch --option substituters "${lib.concatStringsSep " " cfg.substituters}" \
            --option trusted-public-keys "" || {
            echo "nix-cache-update: failed to switch to new generation"
            exit 1
          }

          echo "$NEW_GEN" > "$STATE_FILE"
          echo "nix-cache-update: successfully switched to generation $NEW_GEN"
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

      tmpfiles.rules = [
        "d ${cfg.cacheMount} 0755 root root -"
        "d /var/lib/nix-cache-client 0755 root root -"
      ];
    };
  };
}
