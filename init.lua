local lazypath = vim.fn.stdpath("data") .. "/lazy/lazy.nvim"
if not vim.loop.fs_stat(lazypath) then
  vim.fn.system({
      "git", "clone", "--filter=blob:none", "https://github.com/folke/lazy.nvim.git", "--branch=stable",
      lazypath,
  })
end
vim.opt.rtp:prepend(lazypath)

require("lazy").setup({
  {
    "folke/tokyonight.nvim",
    priority = 1000,
  },
  {
    "ethanholz/nvim-lastplace",
    config = function()
      require("nvim-lastplace").setup()
    end,
  },
  {
    "zbirenbaum/copilot.lua",
    config = function()
      require("copilot").setup({
        panel = { enabled = false },
        suggestion = { enabled = false },
      })
    end,
  },
  { "williamboman/mason.nvim" },
  {
    "nvim-treesitter/nvim-treesitter",
    build = ":TSUpdate",
    config = function()
      require("nvim-treesitter.configs").setup({
        highlight = { enable = true },
        indent = { enable = true },
      })
    end,
  },
  { "neovim/nvim-lspconfig" },
  {
    "nvim-telescope/telescope.nvim",
    dependencies = { "nvim-lua/plenary.nvim" },
  },
  {
    "hrsh7th/nvim-cmp",
    config = function()
      local cmp = require("cmp")
      cmp.setup({
        mapping = {
          ["<C-n>"] = cmp.mapping.select_next_item { behavior = cmp.SelectBehavior.Insert },
          ["<C-p>"] = cmp.mapping.select_prev_item { behavior = cmp.SelectBehavior.Insert },
          ["<C-y>"] = cmp.mapping(
            cmp.mapping.confirm {
              behavior = cmp.ConfirmBehavior.Insert,
              select = true,
            }, { "i", "c" }
          ),
        },
        snippet = {
          expand = function(args)
            vim.snippet.expand(args.body)
          end,
        },
        sources = {
          { name = "buffer" },
          { name = "copilot" },
          { name = "nvim_lsp" },
          { name = "path" },
          { name = "treesitter" },
        },
      })
    end,
  },
  { "hrsh7th/cmp-buffer" },
  {
    "zbirenbaum/copilot-cmp",
    config = function()
      require("copilot_cmp").setup()
    end,
  },
  { "hrsh7th/cmp-nvim-lsp" },
  { "hrsh7th/cmp-path" },
})

vim.cmd.colorscheme("tokyonight-night")
vim.g.mapleader = " "
vim.keymap.set("n", "H", ":bprev<CR>")
vim.keymap.set("n", "L", ":bnext<CR>")
vim.opt.clipboard = "unnamedplus"
vim.opt.colorcolumn = "80"
vim.opt.expandtab = true
vim.opt.guicursor = ""
vim.opt.number = true
vim.opt.relativenumber = true
vim.opt.scrolloff = 8
vim.opt.shiftwidth = 4
vim.opt.smartindent = true
vim.opt.softtabstop = 4
vim.opt.tabstop = 4

local servers = {
  bashls = {},
  golangci_lint_ls = {},
  gopls = {
    gopls = {
      gofumpt = true,
    },
  },
  jsonls = {},
  yamlls = {},
}
local capabilities = vim.lsp.protocol.make_client_capabilities()
capabilities = vim.tbl_deep_extend("force", capabilities, require("cmp_nvim_lsp").default_capabilities())
local lspconfig = require("lspconfig")
for server, settings in pairs(servers) do
  lspconfig[server].setup({
    capabilities = capabilities,
    settings = settings,
  })
end

require("telescope").setup()
local builtin = require("telescope.builtin")
vim.keymap.set("n", "<leader>b", builtin.buffers)
vim.keymap.set("n", "<leader>f", builtin.find_files)
vim.keymap.set("n", "<leader>g", builtin.live_grep)

vim.api.nvim_create_autocmd("BufWritePre", {
  callback = function()
    vim.lsp.buf.format()
  end,
})
vim.api.nvim_create_autocmd("LspAttach", {
  callback = function()
    local builtin = require("telescope.builtin")
    vim.keymap.set("n", "gd", builtin.lsp_definitions)
    vim.keymap.set("n", "gi", builtin.lsp_implementations)
    vim.keymap.set("n", "gr", builtin.lsp_references)
    vim.keymap.set("n", "gt", builtin.lsp_type_definitions)
    vim.keymap.set("n", "K", vim.lsp.buf.hover, { silent = false })
    vim.keymap.set("n", "<leader>j", vim.diagnostic.goto_prev, { silent = false })
    vim.keymap.set("n", "<leader>k", vim.diagnostic.goto_next, { silent = false })
    vim.keymap.set("n", "<leader>r", vim.lsp.buf.rename, { silent = false })
  end,
})
