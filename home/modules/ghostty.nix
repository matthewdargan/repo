{inputs, ...}: {pkgs, ...}: {
  home.packages = [
    inputs.ghostty.packages.${pkgs.system}.default
    pkgs.comic-mono
  ];
  xdg.configFile."ghostty/config".text = ''
    font-family = "Comic Mono"
    font-size = 20
    fullscreen = true
    mouse-hide-while-typing = true
    theme = "tokyonight"
    window-decoration = false
  '';
}
