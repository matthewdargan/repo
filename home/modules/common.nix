{
  lib,
  pkgs,
  ...
}: {
  home.packages = [pkgs.terraform];
  home.stateVersion = "23.11";
  nixpkgs.config.allowUnfree = true;
  programs = {
    bash = {
      enable = true;
      historyControl = ["ignoredups" "ignorespace"];
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
      sessionVariables = {
        EDITOR = "nvim";
        VISUAL = "nvim";
      };
      shellAliases = {
        ls = "ls -h --color=auto";
        ll = "ls -alF";
        vim = "nvim";
      };
    };
    direnv = {
      enable = true;
      nix-direnv.enable = true;
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
    go = {
      enable = true;
      package = pkgs.go_1_22;
    };
    nixvim = {
      enable = true;
      clipboard.register = "unnamedplus";
      colorschemes.kanagawa = {
        enable = true;
        theme = "wave";
      };
      globals.mapleader = " ";
      keymaps = [
        {
          action = ":bnext<CR>";
          key = "L";
          mode = ["n"];
        }
        {
          action = ":bprev<CR>";
          key = "H";
          mode = ["n"];
        }
      ];
      options = {
        colorcolumn = "80";
        expandtab = true;
        guicursor = "";
        mouse = "a";
        number = true;
        relativenumber = true;
        scrolloff = 8;
        shiftwidth = 4;
        smartindent = true;
        softtabstop = 4;
        spelllang = "en_us";
        spell = true;
        tabstop = 4;
      };
      plugins = {
        cmp = {
          enable = true;
          settings = {
            mapping = {
              "<CR>" = "cmp.mapping.confirm({ select = true })";
              "<S-Tab>" = "cmp.mapping(cmp.mapping.select_prev_item(), {'i', 's'})";
              "<Tab>" = "cmp.mapping(cmp.mapping.select_next_item(), {'i', 's'})";
            };
            snippet.expand = "function(args) require('luasnip').lsp_expand(args.body) end";
            sources = [
              {name = "buffer";}
              {name = "luasnip";}
              {name = "nvim_lsp";}
              {name = "path";}
              {name = "treesitter";}
            ];
          };
        };
        lastplace.enable = true;
        lsp = {
          enable = true;
          keymaps = {
            diagnostic = {
              "<leader>j" = "goto_prev";
              "<leader>k" = "goto_next";
            };
            lspBuf = {
              "K" = "hover";
              "<leader>r" = "rename";
            };
          };
          servers = {
            bashls.enable = true;
            gopls.enable = true;
            jsonls.enable = true;
            nil_ls.enable = true;
            terraformls.enable = true;
            yamlls.enable = true;
          };
        };
        lsp-format.enable = true;
        luasnip.enable = true;
        none-ls = {
          enable = true;
          enableLspFormat = true;
          sources = {
            diagnostics.golangci_lint.enable = true;
            formatting = {
              alejandra.enable = true;
              gofumpt.enable = true;
            };
          };
        };
        telescope = {
          enable = true;
          keymaps = {
            "<leader>b" = "buffers";
            "<leader>f" = "find_files";
            "<leader>g" = "live_grep";
            "gd" = "lsp_definitions";
            "gi" = "lsp_implementations";
            "gr" = "lsp_references";
            "gt" = "lsp_type_definitions";
          };
        };
        treesitter = {
          enable = true;
          ensureInstalled = "all";
          moduleConfig = {
            highlight.enable = true;
            indent.enable = true;
          };
          nixGrammars = true;
        };
      };
    };
    readline = {
      enable = true;
      bindings = {
        "\\C-[[A" = "history-search-backward";
        "\\C-[[B" = "history-search-forward";
      };
    };
    ripgrep.enable = true;
  };
  xdg.enable = true;
}
