{inputs, ...}: {pkgs, ...}: {
  home.packages = [pkgs.comic-mono];
  programs.wezterm = {
    enable = true;
    extraConfig = ''
      local wezterm = require 'wezterm'
      local config = wezterm.config_builder()
      config.color_scheme = 'tokyonight_night'
      config.font = wezterm.font('Comic Mono')
      config.font_size = 20.0
      config.hide_tab_bar_if_only_one_tab = true
      local mux = wezterm.mux

      wezterm.on('gui-startup', function(cmd)
        local tab, pane, window = mux.spawn_window(cmd or {})
        window:gui_window():maximize()
      end)

      return config
    '';
    package = inputs.wezterm.packages.${pkgs.system}.default;
  };
}
