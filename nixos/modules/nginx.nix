{
  config,
  lib,
  ...
}:
with lib; let
  cfg = config.services.nginx-reverse-proxy;
in {
  options.services.nginx-reverse-proxy = {
    enable = mkEnableOption "nginx reverse proxy with ACME";

    domain = mkOption {
      type = types.str;
      description = "Domain name for the site";
    };

    authPort = mkOption {
      type = types.port;
      default = 8080;
      description = "Port where authd listens";
    };

    jellyfinSubdomain = mkOption {
      type = types.str;
      default = "jellyfin";
      description = "Subdomain for Jellyfin (e.g., 'jellyfin' becomes 'jellyfin.domain.com')";
    };

    jellyfinHost = mkOption {
      type = types.str;
      default = "nas";
      description = "Jellyfin backend hostname";
    };

    jellyfinPort = mkOption {
      type = types.port;
      default = 8096;
      description = "Jellyfin backend port";
    };

    publicRoot = mkOption {
      type = types.str;
      description = "Directory for public files";
    };

    privateRoot = mkOption {
      type = types.str;
      description = "Directory for private files (requires auth)";
    };

    email = mkOption {
      type = types.str;
      description = "Email address for Let's Encrypt notifications";
    };
  };

  config = mkIf cfg.enable {
    services.nginx = {
      enable = true;
      recommendedGzipSettings = true;
      recommendedOptimisation = true;
      recommendedTlsSettings = true;
      clientMaxBodySize = "20M";

      virtualHosts.${cfg.domain} = {
        forceSSL = true;
        enableACME = true;

        locations = {
          "= /auth" = {
            proxyPass = "http://127.0.0.1:${toString cfg.authPort}";
            extraConfig = ''
              internal;
              proxy_pass_request_body off;
              proxy_set_header Content-Length "";
              proxy_set_header X-Original-URI $request_uri;
              proxy_set_header X-Real-IP $remote_addr;
            '';
          };

          "= /login" = {
            extraConfig = ''
              if ($request_method = GET) {
                rewrite ^ /login.html last;
              }
              proxy_pass http://127.0.0.1:${toString cfg.authPort};
              proxy_set_header X-Real-IP $remote_addr;
            '';
          };

          "= /logout" = {
            proxyPass = "http://127.0.0.1:${toString cfg.authPort}";
          };

          "@auth_error" = {
            extraConfig = ''
              return 302 /login?redirect=$request_uri;
            '';
          };

          "= /private" = {
            extraConfig = ''
              return 301 /private/;
            '';
          };

          "/private/" = {
            alias = "${cfg.privateRoot}/";
            extraConfig = ''
              auth_request /auth;
              error_page 401 = @auth_error;
              autoindex on;
            '';
          };

          "/" = {
            root = cfg.publicRoot;
            extraConfig = ''
              try_files $uri $uri/ =404;
            '';
          };
        };
      };

      virtualHosts."${cfg.jellyfinSubdomain}.${cfg.domain}" = {
        forceSSL = true;
        enableACME = true;

        locations."/" = {
          proxyPass = "http://${cfg.jellyfinHost}:${toString cfg.jellyfinPort}";
          proxyWebsockets = true;
          extraConfig = ''
            proxy_buffering off;
            proxy_set_header Host $host;
            proxy_set_header X-Real-IP $remote_addr;
            proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
            proxy_set_header X-Forwarded-Proto $scheme;
            proxy_set_header X-Forwarded-Host $http_host;
            proxy_set_header Range $http_range;
            proxy_set_header If-Range $http_if_range;
            proxy_redirect off;
          '';
        };
      };
    };

    security.acme = {
      acceptTerms = true;
      defaults.email = cfg.email;
    };

    networking.firewall.allowedTCPPorts = [80 443];
  };
}
