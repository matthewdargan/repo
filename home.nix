{
  config,
  pkgs,
  ...
}: {
  home.username = "mpd";
  home.homeDirectory = "/home/mpd";
  home.stateVersion = "23.11";
  home.packages = [pkgs.alejandra pkgs.go_1_22 pkgs.terraform];
  nixpkgs.config.allowUnfree = true;
  programs = {
    bash = {
      enable = true;
      historyControl = ["ignoredups" "ignorespace"];
      sessionVariables = {
        PATH = "$HOME/bin:$GOPATH/bin:$PATH";
        EDITOR = "nvim";
        VISUAL = "nvim";
      };
      shellAliases = {
        ls = "ls -h --color=auto";
        ll = "ls -alF";
        vim = "nvim";
      };
      initExtra = ''
        bind '"\e[A": history-search-backward'
        bind '"\e[B": history-search-forward'
        set -o vi
      '';
    };
    direnv = {
      enable = true;
      nix-direnv.enable = true;
    };
  };
}
