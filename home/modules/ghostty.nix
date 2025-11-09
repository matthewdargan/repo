{
  inputs,
  pkgs,
  ...
}: {
  home.packages = [
    inputs.ghostty.packages.${pkgs.system}.default
    pkgs.liberation_ttf
  ];
  xdg.configFile."ghostty/config".text = ''
    font-family = "Liberation Mono"
    font-size = 20
    fullscreen = true
    mouse-hide-while-typing = true
    theme = TokyoNight
    window-decoration = false
  '';
}
