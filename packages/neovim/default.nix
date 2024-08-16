_: {
  perSystem = {
    inputs',
    pkgs,
    ...
  }: let
    inherit (inputs'.nix-go.packages) gopls;
  in {
    packages.neovim = inputs'.nixvim.legacyPackages.makeNixvimWithModule {
      module = {helpers, ...}: {
        autoCmd = [
          {
            callback = helpers.mkRaw "function() vim.lsp.buf.format() end";
            event = ["BufWritePre"];
          }
        ];
        clipboard = {
          providers.wl-copy.enable = true;
          register = "unnamedplus";
        };
        colorschemes.tokyonight = {
          enable = true;
          settings.style = "night";
        };
        extraPackages = [pkgs.ripgrep];
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
          colorcolumn = "80";
          expandtab = true;
          guicursor = "";
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
                "<C-n>" = "cmp.mapping.select_next_item { behavior = cmp.SelectBehavior.Insert }";
                "<C-p>" = "cmp.mapping.select_prev_item { behavior = cmp.SelectBehavior.Insert }";
                "<C-y>" = ''cmp.mapping(cmp.mapping.confirm { behavior = cmp.ConfirmBehavior.Insert, select = true }, { "i", "c" })'';
              };
              snippet.expand = "function(args) require('luasnip').lsp_expand(args.body) end";
              sources = [
                {name = "buffer";}
                {name = "copilot";}
                {name = "luasnip";}
                {name = "nvim_lsp";}
                {name = "path";}
                {name = "treesitter";}
              ];
            };
          };
          copilot-lua = {
            enable = true;
            panel.enabled = false;
            suggestion.enabled = false;
          };
          lastplace.enable = true;
          lsp = {
            enable = true;
            keymaps = {
              diagnostic = {
                "<leader>j" = "goto_prev";
                "<leader>k" = "goto_next";
              };
              lspBuf."<leader>r" = "rename";
            };
            servers = {
              bashls.enable = true;
              golangci-lint-ls.enable = true;
              gopls = {
                enable = true;
                package = gopls;
                settings.gofumpt.enable = true;
              };
              nil-ls.enable = true;
              terraformls.enable = true;
              yamlls.enable = true;
            };
          };
          luasnip.enable = true;
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
            settings = {
              highlight.enable = true;
              indent.enable = true;
            };
          };
        };
      };
    };
  };
}
