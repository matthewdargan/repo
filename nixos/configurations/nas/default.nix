{
  config,
  pkgs,
  self,
  ...
}: let
  mounts = [
    {
      what = "127.0.0.1";
      where = "/home/media/n/nas";
      type = "9p";
      options = "port=4500";
      requires = ["9pfs.service"];
      after = ["9pfs.service" "network-online.target"];
      wants = ["network-online.target"];
    }
    {
      what = "/home/media/n/nas/movies";
      where = "/home/media/n/movies";
      type = "none";
      options = "bind";
      requires = ["home-media-n-nas.mount"];
      after = ["home-media-n-nas.mount"];
    }
    {
      what = "/home/media/n/nas/shows";
      where = "/home/media/n/shows";
      type = "none";
      options = "bind";
      requires = ["home-media-n-nas.mount"];
      after = ["home-media-n-nas.mount"];
    }
    {
      what = "127.0.0.1";
      where = "/mnt/nix-cache";
      type = "9p";
      options = "port=9564";
      requires = ["nix-cache-serve.service"];
      after = ["nix-cache-serve.service" "network-online.target"];
      wants = ["network-online.target"];
    }
  ];
in {
  imports = [
    ./hardware.nix
    self.nixosModules."9p-tools"
    self.nixosModules.fish
    self.nixosModules.git-server
    self.nixosModules.locale
    self.nixosModules.nix-cache-client
    self.nixosModules.nix-config
    self.nixosModules.settings
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
      allowedTCPPorts = [4500 9564];
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

          FLAKE=/srv/git/repo
          CONFIGS=$(${pkgs.nix}/bin/nix eval --json "$FLAKE#nixosConfigurations" --apply builtins.attrNames | ${pkgs.jq}/bin/jq -r '.[]')

          for config in $CONFIGS; do
            STORE_PATH=$(${pkgs.nix}/bin/nix build "$FLAKE#nixosConfigurations.$config.config.system.build.toplevel" --no-link --print-out-paths 2>&1 | tail -1)
            [[ -z "$STORE_PATH" ]] && continue

            ${pkgs.nix}/bin/nix copy --to file:///srv/nix-cache --no-check-sigs "$STORE_PATH"

            CONFIG_DIR=/srv/nix-cache/configs/$config
            mkdir -p "$CONFIG_DIR"
            CURRENT_GEN=$(cat "$CONFIG_DIR/generation" 2>/dev/null || echo 0)
            NEW_GEN=$((CURRENT_GEN + 1))

            echo "$NEW_GEN" > "$CONFIG_DIR/generation"
            echo "$STORE_PATH" > "$CONFIG_DIR/path"
            date -Iseconds > "$CONFIG_DIR/timestamp"
          done

          git --git-dir=/srv/git/repo.git push --mirror github 2>&1 || true
        fi
      '';
    };
    jellyfin = {
      enable = true;
      cacheDir = "/home/media/.cache/jellyfin";
      dataDir = "/home/media/jellyfin";
      openFirewall = true;
      user = "media";
    };
    nix-cache-client.enable = true;
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
      jellyfin = {
        after = ["home-media-n-movies.automount" "home-media-n-shows.automount"];
        wants = ["home-media-n-movies.automount" "home-media-n-shows.automount"];
      };
      "9pfs" = {
        after = ["network.target"];
        description = "9P filesystem server (debug)";
        serviceConfig = {
          ExecStart = "${self.packages.${pkgs.stdenv.hostPlatform.system}."9pfs-debug"}/bin/9pfs --root=/media tcp!*!4500";
          Restart = "always";
          RestartSec = "5s";
          StandardError = "journal";
          StandardOutput = "journal";
          User = "storage";
        };
        wantedBy = ["multi-user.target"];
      };
      "9p-health-check" = {
        description = "Check 9P mount health and restart if stale";
        serviceConfig = {
          Type = "oneshot";
          User = "media";
        };
        script = ''
          if ${pkgs.systemd}/bin/systemctl is-active --quiet home-media-n-nas.mount; then
            if ! ${pkgs.coreutils}/bin/timeout 5 ${pkgs.coreutils}/bin/stat /home/media/n/nas >/dev/null 2>&1; then
              echo "9P mount at /home/media/n/nas is unresponsive, restarting..."
              ${pkgs.systemd}/bin/systemctl restart home-media-n-nas.mount
            fi
          fi
        '';
      };
      nix-cache-serve = {
        description = "9P server for nix binary cache";
        after = ["network.target"];
        wantedBy = ["multi-user.target"];
        serviceConfig = {
          Type = "simple";
          User = "git";
          Group = "git";
          ExecStart = "${self.packages.${pkgs.stdenv.hostPlatform.system}."9pfs"}/bin/9pfs --root=/srv/nix-cache tcp!*!9564";
          Restart = "always";
          RestartSec = "10s";
          StandardOutput = "journal";
          StandardError = "journal";
        };
      };
    };
    timers."9p-health-check" = {
      wantedBy = ["timers.target"];
      timerConfig = {
        OnBootSec = "5min";
        OnUnitActiveSec = "5min";
      };
    };
    tmpfiles.rules = [
      "d /srv/nix-cache 0755 git git -"
      "d /srv/nix-cache/configs 0755 git git -"
    ];
  };
  system.stateVersion = "25.05";
  users = {
    groups.media = {};
    groups.storage = {};
    users = {
      media = {
        extraGroups = ["systemd-journal"];
        group = "media";
        isNormalUser = true;
        linger = true;
        packages = [self.packages.${pkgs.stdenv.hostPlatform.system}.neovim];
        shell = pkgs.fish;
      };
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
