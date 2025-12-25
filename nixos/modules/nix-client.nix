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
      default = "/var/lib/nix-client/n/nix";
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

    maxHistoryEntries = lib.mkOption {
      type = lib.types.int;
      default = 10;
      description = "Maximum number of historical configurations to track for rollback";
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
                    HISTORY_DIR=$STATE_DIR/history
                    CURRENT_STATE=$STATE_DIR/current
                    HOST_DIR="${cfg.cacheMount}/hosts/${cfg.hostname}"
                    CURRENT_LINK="$HOST_DIR/current"
                    PUBLIC_KEY="${cfg.cacheMount}/keys/cache-public-key"

                    mkdir -p "$STATE_DIR" "$HISTORY_DIR"

                    [ ! -e "$CURRENT_LINK" ] && exit 0
                    [ ! -f "$PUBLIC_KEY" ] && exit 1

                    NEW_SYSTEM=$(cat "$CURRENT_LINK/system")
                    [ -z "$NEW_SYSTEM" ] && exit 1

                    CURRENT_SYSTEM=$(readlink -f /nix/var/nix/profiles/system || echo "")
                    [ "$NEW_SYSTEM" = "$CURRENT_SYSTEM" ] && exit 0

                    METADATA_FILE="$CURRENT_LINK/metadata"
                    [ ! -f "$METADATA_FILE" ] && exit 1

                    GIT_COMMIT=$(grep '^git_commit: ' "$METADATA_FILE" | cut -d' ' -f2)
                    BUILD_TIME=$(grep '^build_timestamp: ' "$METADATA_FILE" | cut -d' ' -f2)
                    NIXOS_VER=$(grep '^nixos_version: ' "$METADATA_FILE" | cut -d' ' -f2)

                    echo "$NEW_SYSTEM"

                    nix copy --from "file://${cfg.cacheMount}" "$NEW_SYSTEM"
                    nix store verify --trusted-public-keys "$(cat "$PUBLIC_KEY")" "$NEW_SYSTEM"

                    [ -f "$CURRENT_STATE" ] && cp "$CURRENT_STATE" "$HISTORY_DIR/$(date +%s)"
                    ls -t "$HISTORY_DIR"/* 2>/dev/null | tail -n +$((${toString cfg.maxHistoryEntries} + 1)) | xargs rm -f || true

                    cat > "$CURRENT_STATE" <<EOF
          store_path: $NEW_SYSTEM
          git_commit: $GIT_COMMIT
          build_timestamp: $BUILD_TIME
          nixos_version: $NIXOS_VER
          activation_timestamp: $(date +%s)
          EOF

                    systemd-run --no-ask-password --unit=nixos-activation "$NEW_SYSTEM/bin/switch-to-configuration" switch
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

      tmpfiles.rules = [
        "d /var/lib/nix-client 0755 root root -"
        "d /var/lib/nix-client/history 0755 root root -"
        "d /var/lib/nix-client/n 0755 root root -"
      ];
    };
  };
}
