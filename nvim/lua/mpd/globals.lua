local globals = {
    mapleader = " ",
}

for k, v in pairs(globals) do
    vim.g[k] = v
end
