_: {
  networking = rec {
    hostId = builtins.substring 0 8 (builtins.hashString "md5" hostName);
    hostName = "router";
    firewall.enable = false;
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
  systemd.network = {
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
          "10.0.0.1/24"
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
}
