{self, ...}: {pkgs, ...}: {
  home = {
    packages = [self.packages.${pkgs.system}.neovim];
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
      shellAliases.vi = "nvim";
    };
    direnv = {
      enable = true;
      nix-direnv.enable = true;
    };
    fish = {
      enable = true;
      interactiveShellInit = ''
        fish_vi_key_bindings
        set -x EDITOR nvim
        set -x VISUAL nvim
      '';
      shellAbbrs.vi = "nvim";
    };
    fzf.enable = true;
    git = {
      enable = true;
      delta.enable = true;
      extraConfig = {
        init.defaultBranch = "main";
        push.autoSetupRemote = true;
      };
      userEmail = "matthewdargan57@gmail.com";
      userName = "Matthew Dargan";
    };
  };
  xdg.enable = true;
}
