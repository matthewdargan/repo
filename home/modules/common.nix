{self, ...}: {
  lib,
  pkgs,
  ...
}: {
  home.packages = [pkgs.terraform pkgs.wl-clipboard];
  home.stateVersion = "23.11";
  nixpkgs.config.allowUnfree = true;
  programs = {
    bash = {
      enable = true;
      historyControl = ["ignoredups" "ignorespace"];
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
      colorschemes.kanagawa.enable = true;
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
        background = "dark";
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
            gopls = {
              enable = true;
              package = self.packages.${pkgs.system}.gopls;
            };
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
