call plug#begin('~/.local/share/nvim/plugged')

Plug 'scrooloose/nerdtree', { 'on': 'NERDTreeToggle' }
Plug 'Shougo/deoplete.nvim', { 'do': ':UpdateRemotePlugins' }
Plug 'tweekmonster/deoplete-clang2'
Plug 'gkjgh/cobalt'
Plug 'vim-airline/vim-airline'
Plug 'vim-airline/vim-airline-themes'
Plug 'edkolev/tmuxline.vim'
Plug 'zchee/deoplete-jedi'

call plug#end()

" General Settings
set number
set expandtab
set backspace=indent,eol,start
set shiftwidth=4
set tabstop=4
set wrap
syntax on

" Airline and Colorscheme
colorscheme cobalt
let g:airline_theme='cobalt'
let g:tmuxline_separators = {
    \ 'left' : '',
    \ 'left_alt': '>',
    \ 'right' : '',
    \ 'right_alt' : '<',
    \ 'space' : ' '}

" NerdTree
map <C-g> :NERDTreeToggle<CR>
let NERDTreeShowHidden = 1

" Deoplete
let g:deoplete#enable_at_startup = 1
inoremap <silent><expr> <Tab> pumvisible() ? "\<C-n>" : "\<Tab>"
inoremap <expr><C-h> deoplete#mappings#smart_close_popup()."\<C-h>"
inoremap <expr><BS> deoplete#mappings#smart_close_popup()."\<C-h>"
set completeopt+=noinsert
inoremap <expr> <CR> pumvisible() ? "\<C-y>" : "\<C-g>u\<CR>"
