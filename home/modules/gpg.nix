{
  lib,
  pkgs,
  ...
}: let
  gpgFingerprint = "8DC7F318428DB775175061814760756816B841E6";
  gpgKeyId = "0x${lib.strings.substring 24 16 gpgFingerprint}";
  import-gpg-yubikey = pkgs.writeShellScriptBin "import-gpg-yubikey" ''
    gpg --batch --yes --command-fd 0 --status-fd 1 --edit-card <<<'fetch'
    echo "${gpgFingerprint}:6:" | gpg --import-ownertrust
    gpg --list-keys
  '';
in {
  home.packages = [import-gpg-yubikey];
  programs = {
    git = {
      signing.key = gpgKeyId;
      settings.commit.gpgsign = true;
    };
    gpg = {
      enable = true;
      scdaemonSettings.disable-ccid = true;
      settings = {
        cert-digest-algo = "SHA512";
        default-preference-list = "SHA512 SHA384 SHA256 AES256 AES192 AES ZLIB BZIP2 ZIP Uncompressed";
        keyid-format = "0xlong";
        personal-cipher-preferences = "AES256 AES192 AES";
        personal-compress-preferences = "ZLIB BZIP2 ZIP Uncompressed";
        personal-digest-preferences = "SHA512 SHA384 SHA256";
        require-cross-certification = true;
        require-secmem = true;
        s2k-cipher-algo = "AES256";
        s2k-digest-algo = "SHA512";
        use-agent = true;
      };
    };
  };
  services.gpg-agent = {
    enable = true;
    enableSshSupport = true;
    defaultCacheTtl = 60;
    extraConfig = "ttyname $GPG_TTY";
    maxCacheTtl = 120;
    pinentry.package = pkgs.pinentry-curses;
  };
}
