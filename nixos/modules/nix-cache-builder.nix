{
  config,
  lib,
  pkgs,
  self,
  ...
}: let
  cfg = config.services.nix-cache-builder;
in {
  options.services.nix-cache-builder = {
    enable = lib.mkEnableOption "nix-cache builder and server";

    cacheDir = lib.mkOption {
      type = lib.types.str;
      default = "/srv/nix-cache";
      description = "Directory to store the binary cache";
    };

    flakePath = lib.mkOption {
      type = lib.types.str;
      default = "/srv/git/repo";
      description = "Path to the flake containing nixosConfigurations";
    };

    port = lib.mkOption {
      type = lib.types.port;
      default = 9564;
      description = "Port for the 9P server";
    };
  };

  config = lib.mkIf cfg.enable {
    systemd = {
      services = {
        nix-cache-serve = {
          description = "9P server for nix binary cache";
          after = ["network.target"];
          wantedBy = ["multi-user.target"];

          serviceConfig = {
            Type = "simple";
            User = "git";
            Group = "git";
            ExecStart = "${self.packages.${pkgs.stdenv.hostPlatform.system}."9pfs"}/bin/9pfs --root=${cfg.cacheDir} tcp!*!${toString cfg.port}";
            Restart = "always";
            RestartSec = "10s";
            StandardOutput = "journal";
            StandardError = "journal";
          };
        };
      };

      # Ensure cache directory exists with correct permissions
      tmpfiles.rules = [
        "d ${cfg.cacheDir} 0755 git git -"
        "d ${cfg.cacheDir}/configs 0755 git git -"
      ];
    };
  };
}
