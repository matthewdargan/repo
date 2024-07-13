{self, ...}: {
  lib,
  pkgs,
  ...
}: {
  home = {
    packages = [pkgs.xclip self.packages.${pkgs.system}.neovim];
    stateVersion = "24.11";
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
    gh = {
      enable = true;
      extensions = [pkgs.gh-copilot];
    };
    git = {
      enable = true;
      delta.enable = true;
      extraConfig = {
        init.defaultBranch = "main";
        push.autoSetupRemote = true;
      };
      userEmail = lib.mkDefault "matthewdargan57@gmail.com";
      userName = "Matthew Dargan";
    };
  };
  xdg.enable = true;
}
