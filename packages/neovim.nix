{
  perSystem = {
    inputs',
    pkgs,
    ...
  }: {
    packages.neovim = inputs'.nixvim.legacyPackages.makeNixvimWithModule {
      module = {
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
              sources = [
                {name = "buffer";}
                {name = "luasnip";}
                {name = "path";}
                {name = "treesitter";}
              ];
            };
          };
          luasnip.enable = true;
          mini = {
            enable = true;
            modules.icons = {};
            mockDevIcons = true;
          };
          telescope = {
            enable = true;
            keymaps = {
              "<leader>b" = "buffers";
              "<leader>f" = "find_files";
              "<leader>g" = "live_grep";
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
