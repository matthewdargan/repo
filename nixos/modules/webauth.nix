{
  config,
  lib,
  pkgs,
  self,
  ...
}: let
  cfg = config.services.webauth;
in {
  options.services.webauth = {
    enable = lib.mkEnableOption "webauth daemon";

    rpId = lib.mkOption {
      type = lib.types.str;
      default = "auth.dargs.dev";
      description = "WebAuthn Relying Party ID";
    };

    rpName = lib.mkOption {
      type = lib.types.str;
      default = "Auth";
      description = "WebAuthn Relying Party display name";
    };

    authSocket = lib.mkOption {
      type = lib.types.str;
      default = "/run/9auth/socket";
      description = "9auth unix socket path";
    };

    sessionDuration = lib.mkOption {
      type = lib.types.int;
      default = 7;
      description = "Session duration in days";
    };

    inviteDuration = lib.mkOption {
      type = lib.types.int;
      default = 24;
      description = "Invite code expiry in hours";
    };

    listenPort = lib.mkOption {
      type = lib.types.port;
      default = 8080;
      description = "HTTP listen port (behind nginx)";
    };
  };

  config = lib.mkIf cfg.enable {
    environment.systemPackages = [
      self.packages.${pkgs.stdenv.hostPlatform.system}.webauth
    ];

    systemd.services.webauth = {
      after = ["network.target" "9auth.service"];
      description = "WebAuthn authentication daemon";
      serviceConfig = {
        ExecStart = "${self.packages.${pkgs.stdenv.hostPlatform.system}.webauth}/bin/webauth --rp-id=${cfg.rpId} --rp-name=${cfg.rpName} --auth-socket=${cfg.authSocket} --session-duration=${toString cfg.sessionDuration} --invite-duration=${toString cfg.inviteDuration} --listen-port=${toString cfg.listenPort}";
        Group = "webauth";
        NoNewPrivileges = true;
        PrivateTmp = true;
        ProtectHome = true;
        ProtectSystem = "strict";
        Restart = "always";
        RestartSec = "5s";
        RestrictSUIDSGID = true;
        SupplementaryGroups = ["9auth"];
        TimeoutStartSec = "10s";
        TimeoutStopSec = "5s";
        User = "webauth";
      };
      wantedBy = ["multi-user.target"];
    };

    users = {
      groups.webauth = {};
      users.webauth = {
        group = "webauth";
        isSystemUser = true;
      };
    };
  };
}
