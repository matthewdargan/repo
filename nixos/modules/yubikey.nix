{
  config,
  lib,
  pkgs,
  ...
}: let
  cfg = config.yubikey;
in {
  options.yubikey = {
    enable = lib.mkEnableOption "YubiKey U2F authentication";
    user = lib.mkOption {
      type = lib.types.str;
      description = "Username for YubiKey authentication";
    };
  };

  config = lib.mkIf cfg.enable {
    environment.systemPackages = [
      pkgs.cryptsetup
      pkgs.yubikey-manager
    ];

    security.pam = {
      services = {
        login.u2fAuth = true;
        sudo.u2fAuth = true;
      };

      u2f = {
        enable = true;
        settings = {
          # generated with: pamu2fcfg -n -o pam://ykey
          authfile = pkgs.writeText "u2f-mappings" (lib.concatStrings [
            cfg.user
            ":9O1EjeGEPs4YHcKknJQ3Ks6uO2POotjGgMSWkvCq71CBVQiyCLx1v6O7a90+JxR5citt9SrZQjGD2HOIwOUZHQ==,DgA9haV6Wvp7d7bw2fPzm6Bhw2Pqa8yw3Ib9nTXs3VW1LrUR2Z48uqd1XJfHd3/u/i61dszsyjL/OIEyjJseVQ==,es256,+presence" # keychain
            ":9XuUd3EOfF5tbiORXs5PbffAKw0OmlqMU/KcKnmwSblVg8Y+qWUeib3kxL4IaytEOPmHSA+BYndVwQgMcphU5Q==,OV40jW9fgPLiKuyp/9rjCVcjsh+JEKquCpjy45OlJl9J7Qfj97Ho+LeMKjQu2JxT4aCpk5+H0IGjk02i6zv4Ng==,es256,+presence" # backup
          ]);
          cue = true;
          origin = "pam://ykey";
        };
      };
    };

    services = {
      pcscd.enable = true;
      udev.packages = [pkgs.yubikey-personalization];
    };
  };
}
