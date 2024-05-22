{inputs, ...}: {
  lib,
  pkgs,
  ...
}: let
  inherit (inputs.nix-go.packages.${pkgs.system}) go;
in {
  home = {
    packages = [pkgs.vim pkgs.xclip];
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
        EDITOR = "vim";
        VISUAL = "vim";
      };
      shellAliases = {
        ll = "ls -alF";
      };
    };
    direnv = {
      enable = true;
      nix-direnv.enable = true;
    };
    fzf.enable = true;
    gh = {
      enable = true;
      extensions = [pkgs.gh-copilot];
    };
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
    vscode.enable = true;
  };
  xdg.enable = true;
}
