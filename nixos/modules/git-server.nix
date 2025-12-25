{
  config,
  lib,
  pkgs,
  ...
}: let
  cfg = config.services.git-server;
in {
  options.services.git-server = {
    enable = lib.mkEnableOption "SSH-based git server for hosting repositories";

    baseDir = lib.mkOption {
      type = lib.types.path;
      default = "/srv/git";
      description = "Base directory for git repositories";
    };

    user = lib.mkOption {
      type = lib.types.str;
      default = "git";
      description = "User for git operations";
    };

    group = lib.mkOption {
      type = lib.types.str;
      default = "git";
      description = "Group for git user";
    };

    authorizedKeys = lib.mkOption {
      type = lib.types.listOf lib.types.str;
      default = [];
      description = "SSH authorized keys for git user";
    };

    extraGroups = lib.mkOption {
      type = lib.types.listOf lib.types.str;
      default = [];
      description = "Additional groups for git user";
    };

    repositories = lib.mkOption {
      type = lib.types.attrsOf (lib.types.submodule {
        options = {
          postReceiveHook = lib.mkOption {
            type = lib.types.nullOr lib.types.path;
            default = null;
            description = "Path to post-receive hook script";
          };
        };
      });
      default = {};
      description = "Git repositories to manage";
    };
  };

  config = lib.mkIf cfg.enable {
    users.users.${cfg.user} = {
      isSystemUser = true;
      inherit (cfg) group extraGroups;
      home = cfg.baseDir;
      createHome = true;
      shell = "${pkgs.git}/bin/git-shell";
      openssh.authorizedKeys.keys = cfg.authorizedKeys;
    };

    users.groups.${cfg.group} = {};

    systemd.tmpfiles.rules =
      ["d ${cfg.baseDir} 0755 ${cfg.user} ${cfg.group} -"]
      ++ lib.flatten (lib.mapAttrsToList (name: repo:
        lib.optionals (repo.postReceiveHook != null) [
          "L+ ${cfg.baseDir}/${name}.git/hooks/post-receive - - - - ${repo.postReceiveHook}"
        ])
      cfg.repositories);
  };
}
