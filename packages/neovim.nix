{
  perSystem = {
    inputs',
    pkgs,
    ...
  }: {
    packages.neovim = inputs'.nixvim.legacyPackages.makeNixvimWithModule {
      module = {helpers, ...}: {
        autoCmd = [
          {
            callback = helpers.mkRaw ''
              function()
                vim.fn.system("ctags -R --fields=+iaS --extra=+q --exclude=.direnv --exclude=.git --exclude=result .")
              end
            '';
            event = ["BufWritePost"];
            pattern = ["*.c" "*.h"];
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
        extraPackages = [
          pkgs.ripgrep
          pkgs.universal-ctags
        ];
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
          number = true;
          relativenumber = true;
          scrolloff = 8;
          shiftwidth = 4;
          smartindent = true;
          softtabstop = 4;
          tabstop = 4;
        };
        plugins = {
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
              "<leader>t" = "tags";
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
