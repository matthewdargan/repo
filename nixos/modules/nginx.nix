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
      recommendedProxySettings = true;
      recommendedTlsSettings = true;

      appendHttpConfig = ''
        proxy_headers_hash_max_size 1024;
        proxy_headers_hash_bucket_size 128;
      '';

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
            '';
          };

          "= /login" = {
            extraConfig = ''
              if ($request_method = GET) {
                rewrite ^ /login.html last;
              }
              proxy_pass http://127.0.0.1:${toString cfg.authPort};
              proxy_set_header Host $host;
              proxy_set_header X-Real-IP $remote_addr;
              proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
              proxy_set_header X-Forwarded-Proto $scheme;
            '';
          };

          "= /logout" = {
            proxyPass = "http://127.0.0.1:${toString cfg.authPort}";
            extraConfig = ''
              proxy_set_header Host $host;
              proxy_set_header X-Real-IP $remote_addr;
              proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
              proxy_set_header X-Forwarded-Proto $scheme;
            '';
          };

          "@auth_error" = {
            extraConfig = ''
              return 302 /login?redirect=$request_uri;
            '';
          };

          "/jellyfin/" = {
            proxyPass = "http://${cfg.jellyfinHost}:${toString cfg.jellyfinPort}/";
            proxyWebsockets = true;
            extraConfig = ''
              auth_request /auth;
              error_page 401 = @auth_error;

              proxy_set_header Host $host;
              proxy_set_header X-Real-IP $remote_addr;
              proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
              proxy_set_header X-Forwarded-Proto $scheme;

              proxy_read_timeout 3600s;
              proxy_send_timeout 3600s;
              proxy_connect_timeout 60s;
              proxy_buffering off;
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
    };

    security.acme = {
      acceptTerms = true;
      defaults.email = cfg.email;
    };

    networking.firewall.allowedTCPPorts = [80 443];
  };
}
