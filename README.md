# My personal dotfiles
## What's included
- Neovim config
- Tmux config
- i3 and i3blocks config
- gitconfig
- bashrc
- Installation script
## Neovim Plugins
I am using [vim-plug](https://github.com/junegunn/vim-plug) as my package manager; it supports parallel fetching, lazy loading, after install hooks, etc...

The plugins I use for neovim include:
- [NERDTree](https://github.com/scrooloose/nerdtree) - A tree explorer plugin for vim
- [Deoplete](https://github.com/Shougo/deoplete.nvim) - An asynchronous autocompletion framework for Neovim
- [Deoplete-clang2](https://github.com/tweekmonster/deoplete-clang2) - A clang completer for Deoplete
- [Deoplete-jedi](https://github.com/zchee/deoplete-jedi) - A Deoplete source for Jedi
- [Cobalt Theme](https://github.com/gkjgh/cobalt) - A Cobalt color scheme for Vim/Neovim
- [Airline](https://github.com/vim-airline/vim-airline) - A status bar for Vim/Neovim
- [Airline Themes](https://github.com/vim-airline/vim-airline-themes) - A collection of themes for Airline
- [Tmuxline](https://github.com/edkolev/tmuxline.vim) - A Tmux status bar that provides integration for Airline
## Installation Guide
Run these commands in the terminal:
```
cd ~/
git clone https://github.com/matthewdargan/dotfiles.git ~/dotfiles
cd ~/dotfiles
./install.sh
```
This will symlink the files in `dotfiles` to their appropriate locations and install the dependencies needed to use the previously mentioned plugins.
Everything is configured and tweaked within `dotfiles`.
You will probably want to change `shell/bashrc`, because it sets a few paths that might be different on your machine.
Also, change `git/gitconfig` unless you're me.
