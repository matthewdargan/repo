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
        extraConfigLua = ''
          vim.api.nvim_create_autocmd("BufWritePost", {
            pattern = {"*.c", "*.h"},
            callback = function()
              vim.fn.jobstart(
                {"ctags", "--langmap=c:+.h", "--languages=C", "--c-kinds=+p", "--fields=+iaS", "--extras=+q", "--recurse", "9p", "auth", "base", "cmd", "http", "json"},
                {cwd = vim.fn.getcwd(), detach = true}
              )
            end
          })
        '';
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
          shiftwidth = 2;
          smartindent = true;
          softtabstop = 2;
          tabstop = 2;
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
              "<leader>s" = "tags";
              "<leader>t" = "current_buffer_tags";
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
