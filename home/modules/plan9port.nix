{
  inputs,
  self,
  ...
}: {pkgs, ...}: {
  home.packages = let
    acme = pkgs.writeShellApplication {
      name = "acme";
      runtimeInputs = [self.packages.${pkgs.system}.plan9port];
      text = ''
        9 acme -a -f /mnt/font/GoRegular/18a/font -F /mnt/font/GoMono/18a/font
      '';
    };
  in [
    acme
    inputs.plan9go.packages.${pkgs.system}.go
    self.packages.${pkgs.system}.plan9port
  ];
}
