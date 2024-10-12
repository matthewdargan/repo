{self, ...}: {pkgs, ...}: {
  home = {
    file.".local/bin/acme" = {
      executable = true;
      text = ''
        #!/usr/bin/env bash
        ${self.packages.${pkgs.system}.plan9port}/bin/9 acme -a -f /mnt/font/GoRegular/18a/font -F /mnt/font/GoMono/18a/font
      '';
    };
    packages = [
      #self.packages.${pkgs.system}."9fans-go"
      self.packages.${pkgs.system}.plan9port
    ];
    sessionPath = ["$HOME/.local/bin"];
  };
}
