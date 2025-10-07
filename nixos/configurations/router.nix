{
  inputs,
  pkgs,
  self,
  ...
}: {
  imports = [inputs.disko.nixosModules.disko];
  boot = {
    kernel.sysctl = {
      "net.ipv4.conf.all.forwarding" = 1;
      "net.ipv4.conf.br-lan.rp_filter" = 1;
      "net.ipv4.conf.default.rp_filter" = 1;
      "net.ipv4.conf.enp3s0.rp_filter" = 1;
    };
    loader = {
      efi.canTouchEfiVariables = true;
      systemd-boot.enable = true;
    };
    supportedFilesystems = ["ext4"];
  };
  disko.devices = {
    disk = {
      main = {
        content = {
          partitions = {
            ESP = {
              content = {
                format = "vfat";
                mountOptions = ["umask=0077"];
                mountpoint = "/boot";
                type = "filesystem";
              };
              end = "1G";
              name = "ESP";
              priority = 1;
              start = "1M";
              type = "EF00";
            };
            root = {
              content = {
                extraArgs = ["-f"];
                mountpoint = "/partition-root";
                subvolumes = {
                  "/home" = {
                    mountOptions = ["compress=zstd"];
                    mountpoint = "/home";
                  };
                  "/nix" = {
                    mountOptions = [
                      "compress=zstd"
                      "noatime"
                    ];
                    mountpoint = "/nix";
                  };
                  "/rootfs".mountpoint = "/";
                };
                swap.swapfile.size = "20M";
                type = "btrfs";
              };
              size = "100%";
            };
          };
          type = "gpt";
        };
        device = "/dev/nvme0n1";
        type = "disk";
      };
    };
  };
  environment.systemPackages = [
    pkgs.iproute2
    self.packages.${pkgs.system}.neovim
  ];
  networking = rec {
    firewall.enable = false;
    hostId = builtins.substring 0 8 (builtins.hashString "md5" hostName);
    hostName = "router";
    networkmanager.enable = false;
    nftables = {
      enable = true;
      preCheckRuleset = ''
        sed 's/.*devices.*/devices = { lo }/g' -i ruleset.conf
        sed -i '/flags offload;/d' -i ruleset.conf
      '';
      tables = {
        "filter" = {
          content = ''
            chain input {
              type filter hook input priority 0; policy drop;
              iifname "br-lan" accept comment "allow local network to access router"
              iifname "enp3s0" ct state { established, related } accept comment "allow established traffic"
              iifname "enp3s0" counter drop comment "drop all other traffic as unsolicited"
              iifname "lo" accept comment "accept traffic from loopback"
            }
            chain forward {
              type filter hook forward priority filter; policy drop;
              iifname "br-lan" oifname "enp3s0" accept comment "allow lan to wan as trusted"
              iifname "enp3s0" oifname "br-lan" ct state { established, related } accept comment "allow established traffic"
              iifname "enp3s0" oifname "br-lan" ct status dnat accept comment "allow nat from wan"
            }
          '';
          family = "inet";
        };
        "nat" = {
          content = ''
            chain prerouting {
              type nat hook prerouting priority -100;
            }
            chain postrouting {
              type nat hook postrouting priority 100; policy accept;
              ip saddr 10.0.0.0/8 oifname "enp3s0" masquerade comment "masquerade private ip addresses"
            }
          '';
          family = "ip";
        };
      };
    };
    useNetworkd = true;
  };
  programs = {
    fish = {
      enable = true;
      interactiveShellInit = ''
        set -U --erase fish_greeting
        fish_vi_key_bindings
        set -gx EDITOR nvim

        # color palette
        set -l foreground c0caf5
        set -l selection 283457
        set -l comment 565f89
        set -l red f7768e
        set -l orange ff9e64
        set -l yellow e0af68
        set -l green 9ece6a
        set -l purple 9d7cd8
        set -l cyan 7dcfff
        set -l pink bb9af7

        # syntax highlighting colors
        set -g fish_color_normal $foreground
        set -g fish_color_command $cyan
        set -g fish_color_keyword $pink
        set -g fish_color_quote $yellow
        set -g fish_color_redirection $foreground
        set -g fish_color_end $orange
        set -g fish_color_option $pink
        set -g fish_color_error $red
        set -g fish_color_param $purple
        set -g fish_color_comment $comment
        set -g fish_color_selection --background=$selection
        set -g fish_color_search_match --background=$selection
        set -g fish_color_operator $green
        set -g fish_color_escape $pink
        set -g fish_color_autosuggestion $comment

        # completion pager colors
        set -g fish_pager_color_progress $comment
        set -g fish_pager_color_prefix $cyan
        set -g fish_pager_color_completion $foreground
        set -g fish_pager_color_description $comment
        set -g fish_pager_color_selected_background --background=$selection
      '';
      shellAbbrs.vi = "nvim";
    };
  };
  security.wrappers = {
    "9bind" = {
      owner = "root";
      group = "root";
      permissions = "u+rx,g+x,o+x";
      setuid = true;
      source = "${self.packages.${pkgs.system}."9bind"}/bin/9bind";
    };
    "9mount" = {
      owner = "root";
      group = "root";
      permissions = "u+rx,g+x,o+x";
      setuid = true;
      source = "${self.packages.${pkgs.system}."9mount"}/bin/9mount";
    };
    "9umount" = {
      owner = "root";
      group = "root";
      permissions = "u+rx,g+x,o+x";
      setuid = true;
      source = "${self.packages.${pkgs.system}."9umount"}/bin/9umount";
    };
  };
  services = {
    kea.dhcp4 = {
      enable = true;
      settings = {
        control-socket = {
          socket-name = "/run/kea/kea-dhcp4.sock";
          socket-type = "unix";
        };
        interfaces-config.interfaces = ["br-lan"];
        lease-database = {
          name = "/var/lib/kea/dhcp4.leases";
          persist = true;
          type = "memfile";
        };
        rebind-timer = 3600 * 2;
        renew-timer = 3600;
        subnet4 = [
          {
            id = 100;
            option-data = [
              {
                data = "10.0.0.1";
                name = "routers";
              }
              {
                data = "10.0.0.1";
                name = "domain-name-servers";
              }
            ];
            pools = [{pool = "10.0.0.100 - 10.0.0.240";}];
            subnet = "10.0.0.0/8";
          }
        ];
        valid-lifetime = 3600 * 4;
      };
    };
    openssh = {
      enable = true;
      settings.PermitRootLogin = "no";
    };
    resolved.enable = false;
    tailscale.enable = true;
    unbound = {
      enable = true;
      package = pkgs.unbound-full;
      resolveLocalQueries = true;
      settings = {
        forward-zone = [
          {
            forward-addr = [
              "1.1.1.1@853#cloudflare-dns.com"
              "1.0.0.1@853#cloudflare-dns.com"
              "8.8.8.8@853#dns.google"
              "8.8.4.4@853#dns.google"
            ];
            forward-first = false;
            forward-tls-upstream = true;
            name = ".";
          }
        ];
        remote-control = {
          control-enable = true;
          control-interface = "/var/run/unbound/unbound.sock";
        };
        server = {
          access-control = [
            "0.0.0.0/0 refuse"
            "127.0.0.0/8 allow"
            "192.168.0.0/16 allow"
            "172.16.0.0/12 allow"
            "10.0.0.0/8 allow"
          ];
          extended-statistics = true;
          interface = [
            "10.0.0.1"
            "127.0.0.1"
          ];
          root-hints = "${pkgs.dns-root-data}/root.hints";
        };
      };
    };
  };
  systemd = {
    network = {
      netdevs."20-br-lan".netdevConfig = {
        Kind = "bridge";
        Name = "br-lan";
      };
      networks = let
        mkLan = Name: {
          linkConfig.RequiredForOnline = "enslaved";
          matchConfig = {inherit Name;};
          networkConfig = {
            Bridge = "br-lan";
            ConfigureWithoutCarrier = true;
          };
        };
      in {
        "10-br-lan" = {
          address = [
            "10.0.0.1/8"
          ];
          bridgeConfig = {};
          linkConfig.RequiredForOnline = "no";
          matchConfig.Name = "br-lan";
          networkConfig = {
            ConfigureWithoutCarrier = true;
            DHCPPrefixDelegation = true;
          };
        };
        "20-wan" = {
          linkConfig.RequiredForOnline = "routable";
          matchConfig.Name = "enp3s0";
          networkConfig = {
            DHCP = "yes";
            DNSOverTLS = true;
            DNSSEC = true;
            IPv4Forwarding = true;
          };
        };
        "30-lan0" = mkLan "enp2s0";
        "30-lan1" = mkLan "enp1s0f0";
        "30-lan2" = mkLan "enp1s0f1";
      };
    };
  };
  system.stateVersion = "25.11";
  time.timeZone = "America/Chicago";
  users.users.mpd = {
    description = "Matthew Dargan";
    extraGroups = [
      "input"
      "networkmanager"
      "systemd-journal"
      "wheel"
    ];
    initialHashedPassword = "$y$j9T$pjjMQ/FjAqPanlHgjkOHA/$/scKw5xPynqGjtdJkh9kh32upTEWKajelv1LtfqdemB";
    isNormalUser = true;
    openssh.authorizedKeys.keys = [
      "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAILe/v2phdFJcaINc1bphWEM6vXDSlXY/e0B2zyb3ik1M matthewdargan57@gmail.com"
    ];
    shell = pkgs.fish;
  };
}
