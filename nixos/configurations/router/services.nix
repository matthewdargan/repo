{pkgs, ...}: {
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
            pools = [{pool = "10.0.0.100 - 10.0.0.254";}];
            reservations = [
              {
                hw-address = "34:5a:60:57:24:e3";
                ip-address = "10.0.0.2";
                hostname = "nas";
              }
              {
                hw-address = "e8:9c:25:6d:01:77";
                ip-address = "10.0.0.3";
                hostname = "scoop";
              }
            ];
            subnet = "10.0.0.0/24";
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
}
