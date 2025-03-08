{self, ...}: {pkgs, ...}: {
  home = {
    packages = [
      (pkgs.vintagestory.override {
        dotnet-runtime_7 = pkgs.dotnet-runtime_7.overrideAttrs (o: {
          src = o.src.overrideAttrs (o: {
            meta =
              o.meta
              // {
                knownVulnerabilities = [];
              };
          });
          meta =
            o.meta
            // {
              knownVulnerabilities = [];
            };
        });
      })
      self.packages.${pkgs.system}.neovim
    ];
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
