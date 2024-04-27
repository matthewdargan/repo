inputs: {
  config,
  lib,
  pkgs,
  ...
}: let
  inherit (inputs.ghlink.packages.${pkgs.system}) ghlink;
  inherit (inputs.nix-go.packages.${pkgs.system}) go gopls;
in {
  home = {
    packages = [pkgs.discord];
    stateVersion = "23.11";
    username = "mpd";
  };
  nixpkgs.config.allowUnfree = true;
  programs = {
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
    nixvim = {
      enable = true;
      autoCmd = [
        {
          callback = config.nixvim.helpers.mkRaw "function() vim.lsp.buf.format() end";
          event = ["BufWritePre"];
        }
      ];
      clipboard = {
        providers = {
          wl-copy.enable = true;
          xclip.enable = true;
        };
        register = "unnamedplus";
      };
      colorschemes.kanagawa.enable = true;
      extraPackages = [ghlink];
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
      opts = {
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
              package = gopls;
            };
            jsonls.enable = true;
            nil_ls.enable = true;
            terraformls.enable = true;
            yamlls.enable = true;
          };
        };
        luasnip.enable = true;
        none-ls = {
          enable = true;
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
    ripgrep.enable = true;
    zsh = {
      enable = true;
      autosuggestion.enable = true;
      defaultKeymap = "viins";
      initExtra = ''
        source ~/.nix-profile/share/git/contrib/completion/git-prompt.sh
        setopt PROMPT_SUBST
        PROMPT='%F{yellow}%n%F{white}:%F{cyan}%~%F{magenta}$(__git_ps1 "(%s)")%F{white}%%%f '
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
  };
  xdg.enable = true;
}
