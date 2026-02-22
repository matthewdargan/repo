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
      description = "Base domain name (e.g., 'dargs.dev')";
    };

    filesRoot = mkOption {
      type = types.str;
      description = "Directory for authenticated files";
    };

    publicRoot = mkOption {
      type = types.str;
      description = "Directory for public files";
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

      virtualHosts = {
        ${cfg.domain} = {
          forceSSL = true;
          enableACME = true;

          locations."/" = {
            root = cfg.publicRoot;
            extraConfig = ''
              try_files $uri $uri/ =404;
            '';
          };
        };

        "auth.${cfg.domain}" = {
          forceSSL = true;
          enableACME = true;

          locations = {
            "= /" = {
              extraConfig = ''
                return 302 /login;
              '';
            };

            "= /validate" = {
              proxyPass = "http://127.0.0.1:8080/auth";
              extraConfig = ''
                internal;
                proxy_pass_request_body off;
                proxy_set_header Content-Length "";
                proxy_set_header X-Original-URI $request_uri;
                proxy_set_header X-Real-IP $remote_addr;
                proxy_set_header X-Forwarded-Host $http_host;
              '';
            };

            "= /login" = {
              proxyPass = "http://127.0.0.1:8080";
              extraConfig = ''
                proxy_set_header X-Real-IP $remote_addr;
                proxy_set_header X-Forwarded-Host $http_host;
              '';
            };

            "= /login/verify" = {
              proxyPass = "http://127.0.0.1:8080";
              extraConfig = ''
                proxy_set_header X-Real-IP $remote_addr;
                proxy_set_header X-Forwarded-Host $http_host;
              '';
            };

            "= /logout" = {
              proxyPass = "http://127.0.0.1:8080";
              extraConfig = ''
                proxy_set_header X-Forwarded-Host $http_host;
              '';
            };

            "/register" = {
              proxyPass = "http://127.0.0.1:8080";
              extraConfig = ''
                proxy_set_header X-Real-IP $remote_addr;
                proxy_set_header X-Forwarded-Host $http_host;
              '';
            };

            "/" = {
              extraConfig = ''
                return 404;
              '';
            };
          };
        };

        "files.${cfg.domain}" = {
          forceSSL = true;
          enableACME = true;

          locations = {
            "@auth_error" = {
              extraConfig = ''
                return 302 https://auth.${cfg.domain}/login?redirect=https://$http_host$request_uri;
              '';
            };

            "/" = {
              root = cfg.filesRoot;
              extraConfig = ''
                auth_request /validate;
                error_page 401 = @auth_error;
                autoindex on;
                try_files $uri $uri/ =404;
              '';
            };

            "= /validate" = {
              proxyPass = "http://127.0.0.1:8080/auth";
              extraConfig = ''
                internal;
                proxy_pass_request_body off;
                proxy_set_header Content-Length "";
                proxy_set_header X-Original-URI $request_uri;
                proxy_set_header X-Real-IP $remote_addr;
                proxy_set_header X-Forwarded-Host $http_host;
              '';
            };
          };
        };

        "jellyfin.${cfg.domain}" = {
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
    };

    security.acme = {
      acceptTerms = true;
      defaults.email = cfg.email;
    };

    networking.firewall.allowedTCPPorts = [80 443];
  };
}
