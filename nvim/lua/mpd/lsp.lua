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

local capabilities = require("cmp_nvim_lsp").default_capabilities()
