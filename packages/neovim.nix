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
        extraPackages = [
          pkgs.clang-tools
          pkgs.ripgrep
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
          lsp = {
            enable = true;
            servers.clangd = {
              enable = true;
              onAttach.function = ''
                vim.keymap.set("n", "gd", vim.lsp.buf.definition, {buffer = bufnr, desc = "Go to definition"})
                vim.keymap.set("n", "gr", vim.lsp.buf.references, {buffer = bufnr, desc = "Go to references"})
                vim.keymap.set("n", "gi", vim.lsp.buf.implementation, {buffer = bufnr, desc = "Go to implementation"})
                vim.keymap.set("n", "K", vim.lsp.buf.hover, {buffer = bufnr, desc = "Hover documentation"})
                vim.keymap.set("n", "<leader>r", vim.lsp.buf.rename, {buffer = bufnr, desc = "Rename symbol"})
                vim.keymap.set("n", "<leader>ca", vim.lsp.buf.code_action, {buffer = bufnr, desc = "Code action"})
                vim.keymap.set("n", "<leader>j", vim.diagnostic.goto_next, {buffer = bufnr, desc = "Next diagnostic"})
                vim.keymap.set("n", "<leader>k", vim.diagnostic.goto_prev, {buffer = bufnr, desc = "Previous diagnostic"})
              '';
            };
          };
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
              "<leader>s" = "lsp_document_symbols";
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
