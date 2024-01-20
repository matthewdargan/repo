local opts = {
    backup = false,
    clipboard = "unnamedplus",
    colorcolumn = "80",
    expandtab = true,
    guicursor = "",
    mouse = "a",
    number = true,
    relativenumber = true,
    scrolloff = 8,
    shiftwidth = 4,
    smartindent = true,
    softtabstop = 4,
    spell = true,
    spelllang = "en_us",
    swapfile = false,
    tabstop = 4,
    termguicolors = true,
    wrap = false,
}

for k, v in pairs(opts) do
    vim.opt[k] = v
end
