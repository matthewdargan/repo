{
  config,
  pkgs,
  ...
}: {
  home.file = {
    ".gitconfig".text = ''
        [user]
            name = Matthew Dargan
            email = matthewdargan57@gmail.com
        [init]
            defaultBranch = main
        [push]
            autoSetupRemote = true
    '';
  };
  home.homeDirectory = "/Users/mdargan";
  home.packages = [pkgs.alejandra pkgs.go_1_22 pkgs.terraform];
  home.stateVersion = "23.11";
  home.username = "mdargan";
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
        function __ps1() {
          local P='$' dir="''${PWD##*/}" B \
            r='\[\e[31m\]' g='\[\e[1;30m\]' h='\[\e[34m\]' \
            u='\[\e[33m\]' p='\[\e[34m\]' w='\[\e[35m\]' \
            b='\[\e[36m\]' x='\[\e[0m\]'

          [[ $EUID == 0 ]] && P='#' && u=$r && p=$u # Root
          [[ $PWD = / ]] && dir=/
          [[ $PWD = "$HOME" ]] && dir='~'

          B=$(git branch --show-current 2>/dev/null)
          [[ $dir = "$B" ]] && B=.
          [[ $B == master || $B == main ]] && b="$r"
          [[ -n "$B" ]] && B="$g($b$B$g)"
          PS1="$u\u$g@$h\h$g:$w$dir$B$p$P$x "
        }
        PROMPT_COMMAND="__ps1"
        set -o vi
      '';
    };
    direnv = {
      enable = true;
      nix-direnv.enable = true;
    };
    readline = {
      enable = true;
      bindings = {
        "\\C-[[A" = "history-search-backward";
        "\\C-[[B" = "history-search-forward";
      };
    };
  };
}
