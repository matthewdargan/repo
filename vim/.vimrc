if has("eval")
    let skip_defaults_vim=1
endif

set nocompatible
set noerrorbells
set autoindent
set autowrite
set number
set ruler
set relativenumber
set tabstop=4 softtabstop=4 shiftwidth=4
set smartindent
set smarttab
set expandtab
set hlsearch
set incsearch
set wrapscan
set textwidth=72
set linebreak
set nobackup
set noswapfile
set nowritebackup
set icon
set viminfo='20,<1000,s1000
set noshowmatch
set wildmenu
set shortmess=aoOtTI
set hidden
set history=100
set ttyfast
filetype plugin indent on

if has("syntax")
    syntax enable
endif

if v:version >= 800
    set nofixendofline
    set listchars=space:*,trail:*,nbsp:*,extends:>,precedes:<,tab:\|>
    set nofoldenable
endif

let mapleader=" "

" Use the formatoptions defaults
set fo-=t   " don't auto-wrap text using text width
set fo+=c   " autowrap comments using textwidth with leader
set fo-=r   " don't auto-insert comment leader on enter in insert
set fo-=o   " don't auto-insert comment leader on o/O in normal
set fo+=q   " allow formatting of comments with gq
set fo-=w   " don't use trailing whitespace for paragraphs
set fo-=a   " disable auto-formatting of paragraph changes
set fo-=n   " don't recognized numbered lists
set fo+=j   " delete comment prefix when joining
set fo-=2   " don't use the indent of second paragraph line
set fo-=v   " don't use broken 'vi-compatible auto-wrapping'
set fo-=b   " don't use broken 'vi-compatible auto-wrapping'
set fo+=l   " long lines not broken in insert mode
set fo+=m   " multi-byte character line break support
set fo+=M   " don't add space before or after multi-byte char
set fo-=B   " don't add space between two multi-byte chars
set fo+=1   " don't break a line after a one-letter word

" Mark trailing spaces as errors
match IncSearch '\s\+$'

set background=dark
set ruf=%30(%=%#LineNr#%.50F\ [%{strlen(&ft)?&ft:'none'}]\ %l:%c\ %p%%%)

" Only load plugins if Plug is detected
if filereadable(expand("~/.vim/autoload/plug.vim"))
    call plug#begin()
    Plug 'morhetz/gruvbox'
    Plug 'fatih/vim-go', { 'do': ':GoInstallBinaries' }
    call plug#end()

    set updatetime=100
    let g:gruvbox_contrast_dark="hard"

    " Golang
    let g:go_fmt_fail_silently=0
    let g:go_fmt_command="goimports"
    let g:go_fmt_autosave=1
    let g:go_gopls_enabled=1
    let g:go_highlight_types=1
    let g:go_highlight_fields=1
    let g:go_highlight_functions=1
    let g:go_highlight_function_calls=1
    let g:go_highlight_operators=1
    let g:go_highlight_extra_types=1
    let g:go_highlight_variable_declarations=1
    let g:go_highlight_variable_assignments=1
    let g:go_highlight_build_constraints=1
    let g:go_highlight_diagnostic_errors=1
    let g:go_highlight_diagnostic_warnings=1
    let g:go_auto_sameids=0
    au FileType go nmap <leader>t :GoTest!<CR>
    au FileType go nmap <leader>b :GoBuild!<CR>
    au FileType go nmap <leader>r :GoRun %<CR>
    au FileType go nmap <leader>i :GoInfo<CR>
    au FileType go nmap <leader>l :GoMetaLinter!<CR>
    au FileType go nmap <leader>n iif err != nil {return err}<CR><ESC>
else
    autocmd BufWritePost *.go !gofmt -w %    " gofmt backup if vim-go fails
endif

" Autoformat options
autocmd BufWritePost *.sh !shfmt -w %

" Make Y consistent with D and C (yank til end)
map Y y$

" Enable omni-completion
set omnifunc=syntaxcomplete#Complete

" Start at last place you were editing
au BufReadPost * if line("'\"") > 1 && line("'\"") <= line("$") | exe "normal! g'\"" | endif

" Functions keys
map <F1> :set number!<CR> :set relativenumber!<CR>
set pastetoggle=<F2>
map <F3> :set list!<CR>
map <F4> :set cursorline!<CR>
map <F5> :set spell!<CR>
map <F6> :set fdm=indent<CR>

nmap <leader>p :set paste<CR>i

" Better use of arrow keys, number increment/decrement
nnoremap <up> <C-a>
nnoremap <down> <C-x>

