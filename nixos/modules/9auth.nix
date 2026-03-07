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
    enableTpm = lib.mkOption {
      type = lib.types.bool;
      default = true;
      description = "Enable TPM 2.0 support";
    };
    authorizedUsers = lib.mkOption {
      type = lib.types.listOf lib.types.str;
      default = [];
      description = "Authorized users";
    };
  };

  config = lib.mkIf cfg.enable {
    boot.kernelModules = lib.mkIf cfg.enableTpm ["tpm_tis" "tpm_crb" "tpm_tis_core"];

    environment.systemPackages =
      [self.packages.${pkgs.stdenv.hostPlatform.system}."9auth"]
      ++ lib.optionals cfg.enableTpm [pkgs.tpm2-tools];

    systemd = {
      services."9auth" = {
        after = ["network.target"];
        wantedBy = ["multi-user.target"];
        description = "9auth authentication daemon with TPM 2.0";

        serviceConfig = {
          # Execution
          ExecStart = "${self.packages.${pkgs.stdenv.hostPlatform.system}."9auth"}/bin/9auth";
          User = "9auth";
          Group = "9auth";
          SupplementaryGroups = lib.mkIf cfg.enableTpm ["tss"];
          Environment = "AUTH_MACHINE_ID_PATH=/etc/machine-id";

          # Lifecycle
          Restart = "always";
          RestartSec = "5s";
          TimeoutStartSec = "10s";
          TimeoutStopSec = "30s";

          # Filesystem
          RuntimeDirectory = "9auth";
          RuntimeDirectoryMode = "0750";
          StateDirectory = "9auth";
          StateDirectoryMode = "0700";

          # Security - Capabilities
          AmbientCapabilities = ["CAP_IPC_LOCK"];
          LimitMEMLOCK = "infinity";
          NoNewPrivileges = true;

          # Security - Device Access
          PrivateDevices = false; # Need TPM access
          DeviceAllow = lib.mkIf cfg.enableTpm ["/dev/tpm0 rw" "/dev/tpmrm0 rw"];

          # Security - Filesystem Isolation
          PrivateTmp = true;
          ProtectHome = true;
          ProtectSystem = "strict";
          ProtectProc = "invisible";
          ProcSubset = "pid";

          # Security - Kernel Protection
          ProtectKernelTunables = true;
          ProtectKernelModules = true;
          ProtectKernelLogs = true;
          ProtectControlGroups = true;
          ProtectClock = true;

          # Security - Network
          RestrictAddressFamilies = ["AF_UNIX"];

          # Security - System Calls
          SystemCallFilter = ["@system-service" "~@privileged" "~@resources"];
          SystemCallArchitectures = "native";

          # Security - Misc Restrictions
          RestrictNamespaces = true;
          RestrictRealtime = true;
          RestrictSUIDSGID = true;
          LockPersonality = true;
          MemoryDenyWriteExecute = true;
          PrivateMounts = true;
          RemoveIPC = true;
          ProtectHostname = true;
          KeyringMode = "private";
        };
      };

      tmpfiles.rules =
        ["f /var/lib/9auth/keys 0600 9auth 9auth -"]
        ++ lib.optionals cfg.enableTpm ["f /var/lib/9auth/tpm-sealed-key 0600 9auth 9auth -"];
    };

    users = {
      groups = {"9auth" = {};} // lib.optionalAttrs cfg.enableTpm {"tss" = {};};
      users =
        {
          "9auth" = {
            group = "9auth";
            isSystemUser = true;
            extraGroups = lib.optional cfg.enableTpm "tss";
          };
        }
        // lib.listToAttrs (map (user: {
            name = user;
            value.extraGroups = ["9auth"];
          })
          cfg.authorizedUsers);
    };

    services.udev.extraRules = lib.mkIf cfg.enableTpm ''
      KERNEL=="tpm[0-9]*", MODE="0660", GROUP="tss"
      KERNEL=="tpmrm[0-9]*", MODE="0660", GROUP="tss"
    '';
  };
}
