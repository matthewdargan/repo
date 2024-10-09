{inputs, ...}: {pkgs, ...}: {
  programs.bash = let
    configFile =
      pkgs.writeText "config.txt"
      ''
        https://go.dev/blog/feed.atom
        https://research.swtch.com/feed.atom
      '';
  in {
    initExtra = ''
      ${inputs.rsspoll.packages.${pkgs.system}.rsspoll}/bin/rsspoll ${configFile} &
    '';
  };
}
