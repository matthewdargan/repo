{
  inputs,
  self,
  ...
}: {pkgs, ...}: {
  home.packages = let
    acme = pkgs.writeShellApplication {
      excludeShellChecks = ["SC2016" "SC2086" "SC2154"];
      name = "acme";
      runtimeInputs = [self.packages.${pkgs.system}.plan9port];
      text = ''
        if ! pgrep -x plumber >/dev/null; then
          9 plumber &
          9 9p write plumb/rules < ${plumbing}
        fi
        EDITOR=editinacme VISUAL=editinacme 9 acme -a -f /mnt/font/GoRegular/18a/font -F /mnt/font/GoMono/18a/font
      '';
    };
    plumbing = builtins.readFile ./plumbing;
  in [
    acme
    inputs.plan9go.packages.${pkgs.system}.go
    self.packages.${pkgs.system}.plan9port
  ];
}
