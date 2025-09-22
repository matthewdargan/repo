{inputs, ...}: {pkgs, ...}: {
  home.packages = [
    inputs.ghostty.packages.${pkgs.system}.default
    pkgs.go-font
  ];
  xdg.configFile."ghostty/config".text = ''
    font-family = "Go Mono"
    font-size = 20
    fullscreen = true
    mouse-hide-while-typing = true
    theme = TokyoNight
    window-decoration = false
  '';
}
