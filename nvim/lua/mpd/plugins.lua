return {
    { "rebelot/kanagawa.nvim", name = "kanagawa", priority = 1000 },
    {
        "nvim-treesitter/nvim-treesitter",
        build = ":TSUpdate",
        config = function () 
            local configs = require("nvim-treesitter.configs")
            configs.setup({
                ensure_installed = { "go", "json", "lua", "terraform", "yaml" },
                sync_install = false,
                highlight = { enable = true },
                indent = { enable = true },  
            })
        end
    },
    { "williamboman/mason.nvim" },
    { "neovim/nvim-lspconfig" },
    { "nvim-telescope/telescope.nvim", dependencies = { "nvim-lua/plenary.nvim" } },
}
