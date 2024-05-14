{
  lib,
  pkgs,
  ...
}: {
  home.packages = [pkgs.discord pkgs.element-desktop];
  nixpkgs.config.allowUnfreePredicate = pkg:
    builtins.elem (lib.getName pkg) [
      "discord"
    ];
}
