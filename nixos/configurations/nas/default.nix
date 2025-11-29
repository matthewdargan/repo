{
  config,
  pkgs,
  self,
  ...
}: let
  mounts = [
    {
      what = "127.0.0.1";
      where = "/n/media";
      type = "9p";
      options = "port=5640";
      requires = ["media-serve.service"];
      after = ["media-serve.service" "network-online.target"];
      wants = ["network-online.target"];
    }
    {
      what = "127.0.0.1";
      where = "/n/nix";
      type = "9p";
      options = "port=5641";
      requires = ["nix-serve.service"];
      after = ["nix-serve.service" "network-online.target"];
      wants = ["network-online.target"];
    }
  ];
in {
  imports = [
    ./hardware.nix
    self.nixosModules."9p-health-check"
    self.nixosModules."9p-tools"
    self.nixosModules.fish
    self.nixosModules.git-server
    self.nixosModules.locale
    self.nixosModules.nix-client
    self.nixosModules.nix-config
  ];
  boot = {
    loader = {
      efi.canTouchEfiVariables = true;
      systemd-boot.enable = true;
    };
    supportedFilesystems = [
      "btrfs"
      "ext4"
      "vfat"
    ];
  };
  networking = rec {
    hostId = builtins.substring 0 8 (builtins.hashString "md5" hostName);
    hostName = "nas";
    firewall = {
      allowedTCPPorts = [5640 5641];
      interfaces.${config.services.tailscale.interfaceName}.allowedTCPPorts = [
        22
        7246
        8096
      ];
    };
    networkmanager.enable = true;
  };
  services = {
    btrfs.autoScrub.enable = true;
    git-server = {
      enable = true;
      baseDir = "/srv/git";
      authorizedKeys = [
        "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAILe/v2phdFJcaINc1bphWEM6vXDSlXY/e0B2zyb3ik1M matthewdargan57@gmail.com"
      ];
      repositories.repo.postReceiveHook = pkgs.writeShellScript "post-receive" ''
        set -euo pipefail

        while read -r _ _ ref; do
          [[ "$ref" == "refs/heads/main" ]] && branch=main
        done

        if [[ -n "''${branch:-}" ]]; then
          git --git-dir=/srv/git/repo.git --work-tree=/srv/git/repo reset --hard "$branch"
          git --git-dir=/srv/git/repo.git --work-tree=/srv/git/repo clean -fd

          # Trigger systemd service to rebuild in background
          mkdir -p /var/lib/git-server
          date -Iseconds > /var/lib/git-server/rebuild-trigger
        fi
      '';
    };
    "9p-health-check" = {
      enable = true;
      mounts = ["n-media" "n-nix"];
    };
    jellyfin = {
      enable = true;
      openFirewall = true;
    };
    nix-client.enable = true;
    openssh = {
      enable = true;
      settings.PermitRootLogin = "no";
    };
    tailscale.enable = true;
  };
  systemd = {
    mounts = map (m: m // {wantedBy = [];}) mounts;
    automounts =
      map (m: {
        inherit (m) where;
        wantedBy = ["multi-user.target"];
        automountConfig.TimeoutIdleSec = "600";
      })
      mounts;
    services = {
      # 9P servers
      media-serve = {
        after = ["network.target"];
        description = "9P server for media files";
        serviceConfig = {
          ExecStart = "${self.packages.${pkgs.stdenv.hostPlatform.system}."9pfs-debug"}/bin/9pfs --root=/media tcp!*!5640";
          Restart = "always";
          RestartSec = "5s";
          StandardError = "journal";
          StandardOutput = "journal";
          User = "storage";
        };
        wantedBy = ["multi-user.target"];
      };
      nix-serve = {
        description = "9P server for nix builds";
        after = ["network.target"];
        wantedBy = ["multi-user.target"];
        serviceConfig = {
          Type = "simple";
          User = "git";
          Group = "git";
          ExecStart = "${self.packages.${pkgs.stdenv.hostPlatform.system}."9pfs"}/bin/9pfs --root=/srv/nix tcp!*!5641";
          Restart = "always";
          RestartSec = "10s";
          StandardOutput = "journal";
          StandardError = "journal";
        };
      };
      # Service dependencies
      jellyfin = {
        after = ["n-media.automount"];
        wants = ["n-media.automount"];
      };

      # Nix rebuild automation
      nix-rebuild = {
        description = "Rebuild all NixOS configurations";
        serviceConfig = {
          Type = "oneshot";
          User = "git";
          Group = "git";
        };
        path = [pkgs.nix pkgs.git pkgs.jq pkgs.coreutils pkgs.openssh];
        script = ''
                    set -euo pipefail

                    FLAKE=/srv/git/repo
                    CONFIGS=$(nix eval --json "$FLAKE#nixosConfigurations" --apply builtins.attrNames | jq -r '.[]')

                    for config in $CONFIGS; do
                      echo "Building $config..."
                      STORE_PATH=$(nix build "$FLAKE#nixosConfigurations.$config.config.system.build.toplevel" --no-link --print-out-paths 2>&1 | tail -1)
                      [[ -z "$STORE_PATH" ]] && continue

                      echo "Copying $config to cache: $STORE_PATH"
                      nix copy --to file:///srv/nix --no-check-sigs "$STORE_PATH"

                      CONFIG_FILE=/srv/nix/configs/$config
                      mkdir -p "$(dirname "$CONFIG_FILE")"

                      CURRENT_GEN=0
                      if [[ -f "$CONFIG_FILE" ]]; then
                        CURRENT_GEN=$(grep '^generation: ' "$CONFIG_FILE" | cut -d' ' -f2)
                      fi
                      NEW_GEN=$((CURRENT_GEN + 1))

                      cat > "$CONFIG_FILE" <<EOF
          generation: $NEW_GEN
          timestamp: $(date +%s)
          path: $STORE_PATH
          EOF
                    done

                    git --git-dir=/srv/git/repo.git push --mirror github 2>&1 || true
        '';
      };
    };
    paths.nix-rebuild = {
      description = "Watch for git push to trigger rebuild";
      wantedBy = ["multi-user.target"];
      pathConfig = {
        PathChanged = "/var/lib/git-server/rebuild-trigger";
      };
    };
    tmpfiles.rules = [
      "d /srv/nix 0755 git git -"
      "d /srv/nix/configs 0755 git git -"
      "d /var/lib/git-server 0755 git git -"
    ];
  };
  system.stateVersion = "25.05";
  users = {
    groups.storage = {};
    users = {
      mpd = {
        description = "Matthew Dargan";
        extraGroups = [
          "input"
          "networkmanager"
          "systemd-journal"
          "wheel"
        ];
        isNormalUser = true;
        openssh.authorizedKeys.keys = [
          "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAILe/v2phdFJcaINc1bphWEM6vXDSlXY/e0B2zyb3ik1M matthewdargan57@gmail.com"
        ];
        shell = pkgs.fish;
      };
      storage = {
        group = "storage";
        isSystemUser = true;
      };
    };
  };
}
