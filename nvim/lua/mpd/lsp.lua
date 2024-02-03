local cmp = require("cmp")
cmp.setup({
    snippet = {
        expand = function(args)
            require("luasnip").lsp_expand(args.body)
        end,
    },
    mapping = {
        ["<Tab>"] = function(fallback)
            if cmp.visible() then
                cmp.select_next_item()
            else
                fallback()
            end
        end,
        ["<CR>"] = function(fallback)
            if cmp.visible() then
                cmp.confirm()
            else
                fallback()
            end
        end
    },
    sources = cmp.config.sources({
        { name = "buffer" },
        { name = "nvim_lsp" },
        { name = "luasnip" },
    })
})

require("mason").setup()
local capabilities = require("cmp_nvim_lsp").default_capabilities()
local on_attach = function(_, _)
    local builtin = require("telescope.builtin")
    vim.keymap.set("n", "gd", builtin.lsp_definitions)
    vim.keymap.set("n", "gi", builtin.lsp_implementations)
    vim.keymap.set("n", "gr", builtin.lsp_references)
    vim.keymap.set("n", "gt", builtin.lsp_type_definitions)
    vim.keymap.set("n", "K", vim.lsp.buf.hover)
    vim.keymap.set("n", "<leader>r", vim.lsp.buf.rename)

    vim.api.nvim_create_autocmd("BufWritePre", {
        callback = function()
            vim.lsp.buf.format { async = false }
        end
    })
end

local lspconfig = require("lspconfig")
lspconfig.gopls.setup {
    capabilities = capabilities,
    on_attach = on_attach,
    settings = {
        gopls = {
            gofumpt = true,
        },
    },
}
lspconfig.golangci_lint_ls.setup {
    capabilities = capabilities,
    on_attach = on_attach,
    settings = {
        gopls = {
            gofumpt = true,
        },
    },
}
lspconfig.jsonls.setup {
    capabilities = capabilities,
    on_attach = on_attach,
}
lspconfig.lua_ls.setup {
    capabilities = capabilities,
    on_attach = on_attach,
    settings = {
        Lua = {
            diagnostics = {
                globals = { "vim" },
            },
        },
    },
}
lspconfig.terraformls.setup {
    capabilities = capabilities,
    on_attach = on_attach,
}
