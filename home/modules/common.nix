{
  inputs,
  self,
  ...
}: {
  config,
  lib,
  pkgs,
  ...
}: let
  inherit (inputs.nix-go.packages.${pkgs.system}) go;
in {
  home = {
    packages = [
      pkgs.discord
      pkgs.element-desktop
      self.packages.${pkgs.system}.neovim
    ];
    stateVersion = "24.05";
    username = "mpd";
  };
  nixpkgs.config.allowUnfree = true;
  programs = {
    bash = {
      enable = true;
      initExtra = ''
        source ~/.nix-profile/share/git/contrib/completion/git-prompt.sh
        PS1='\[\e[1;33m\]\u\[\e[38;5;246m\]:\[\e[1;36m\]\w$(__git_ps1 "\[\e[38;5;246m\](\[\e[1;35m\]%s\[\e[38;5;246m\])")\[\e[1;32m\]$\[\e[0m\] '
        set -o vi
      '';
      sessionVariables = {
        EDITOR = "nvim";
        VISUAL = "nvim";
      };
      shellAliases = {
        ll = "ls -alF";
        vim = "nvim";
      };
    };
    direnv = {
      enable = true;
      nix-direnv.enable = true;
    };
    fzf.enable = true;
    git = {
      enable = true;
      delta.enable = true;
      extraConfig = {
        commit.gpgsign = true;
        init.defaultBranch = "main";
        push.autoSetupRemote = true;
      };
      userEmail = lib.mkDefault "matthewdargan57@gmail.com";
      userName = "Matthew Dargan";
    };
    go = {
      enable = true;
      package = go;
    };
    gpg.enable = true;
    kitty = {
      enable = true;
      font = {
        name = "Go Mono";
        package = pkgs.go-font;
        size = 20;
      };
      settings = {
        active_tab_background = "#1F1F28";
        active_tab_foreground = "#C8C093";
        background = "#1F1F28";
        color0 = "#16161D";
        color10 = "#98BB6C";
        color11 = "#E6C384";
        color12 = "#7FB4CA";
        color13 = "#938AA9";
        color14 = "#7AA89F";
        color15 = "#DCD7BA";
        color16 = "#FFA066";
        color17 = "#FF5D62";
        color1 = "#C34043";
        color2 = "#76946A";
        color3 = "#C0A36E";
        color4 = "#7E9CD8";
        color5 = "#957FB8";
        color6 = "#6A9589";
        color7 = "#C8C093";
        color8 = "#727169";
        color9 = "#E82424";
        cursor = "#C8C093";
        foreground = "#DCD7BA";
        inactive_tab_background = "#1F1F28";
        inactive_tab_foreground = "#727169";
        selection_background = "#2D4F67";
        selection_foreground = "#C8C093";
        url_color = "#72A7BC";
      };
    };
  };
  xdg.enable = true;
}
