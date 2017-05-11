#!/bin/bash

# Don't continue on error
set -e

# Existing files won't be replaced
REPLACE_FILES=false

#-----------------------------------------------------
# Functions and variables
#-----------------------------------------------------
current_path=$(pwd)

command_exists() {
    type "$1" &>/dev/null
}

install_plug_nvim() {
    curl -fLo ~/.local/share/nvim/site/autoload/plug.vim --create-dirs \
        https://raw.githubusercontent.com/junegunn/vim-plug/master/plug.vim
}

install_nvim_folder() {
    install_plug_nvim
    ln -sf $current_path/neovim/init.vim ~/.config/nvim/init.vim
}


#-----------------------------------------------------
# Basic requirements check
#-----------------------------------------------------
if ! command_exists apt-get; then
    echo "This installer is only compatible with debian/ubuntu based Linux distributions."
    echo "Please install configuration files manually."
    exit
fi

if ! command_exists curl; then
    sudo apt-get install -y curl
fi

if ! command_exists git; then
    sudo apt-get install -y git
fi

if ! command_exists pip; then
    sudo apt-get install -y python-pip
fi

# install both pip3 and the jedi package to use deoplete-jedi
if ! command_exists pip3; then
    sudo apt-get install -y python3-pip
    sudo pip2 install jedi
    sudo pip3 install jedi
fi

if ! command_exists clang; then
    sudo apt-get install -y clang
fi

#-----------------------------------------------------
# Git (config, ignore)
#-----------------------------------------------------
echo -n "[ gitconfig ]"

if [ ! -f ~/.gitconfig ]; then
    echo "  Creating gitconfig!"
    ln -sf $current_path/git/gitconfig ~/.gitconfig
elif $REPLACE_FILES; then
    echo "  Deleting old gitconfig!"
    rm ~/.gitconfig
    ln -sf $current_path/git/gitconfig ~/.gitconfig
else
    echo "  Keeping existing gitconfig!"
fi

# TODO: gitignore

#-----------------------------------------------------
# Neovim
#-----------------------------------------------------
echo -n "[ Neovim ]"

if ! command_exists nvim; then
    echo "    Installing Neovim!"
    sudo add-apt-repository ppa:neovim-ppa/unstable
    sudo apt-get update
    sudo apt-get install -y neovim
    sudo pip2 install neovim
    sudo pip3 install neovim
    sudo pip2 install --upgrade neovim
    sudo pip3 install --upgrade neovim
fi

echo -n "[ Neovim config ]"

if [ ! -d ~/.config/nvim ]; then
    echo "    Creating nvim folder!"
    mkdir ~/.config/nvim
    install_nvim_folder
elif $REPLACE_FILES; then
    echo "    Deleting old nvim folder!"
    rm -rf ~/.config/nvim
    install_nvim_folder
else
    echo "    Keeping existing nvim folder!"
fi

#-----------------------------------------------------
# Tmux
#-----------------------------------------------------
echo -n "[ tmux.conf ]"

if ! command_exists tmux; then
    sudo apt-get install tmux -y
fi

if [ ! -f ~/.tmux.conf ]; then
    echo "    Creating tmux.conf!"
    ln -sf $current_path/tmux/tmux.conf ~/.tmux.conf
elif $REPLACE_FILES; then
    echo "    Deleting old tmux.conf!"
    rm ~/.tmux.conf
    ln -sf $current_path/tmux/tmux.conf ~/.tmux.conf
else
    echo "    Keeping existing tmux.conf!"
fi

#-----------------------------------------------------
# Bash installation
#-----------------------------------------------------
echo -n "[ bashrc ]"

if [ ! -f ~/.bashrc ]; then
    echo "    Creating bashrc!"
    ln -sf $current_path/shell/bashrc ~/.bashrc
elif $REPLACE_FILES; then
    echo "    Deleting old bashrc!"
    rm ~/.bashrc
    ln -sf $current_path/shell/bashrc ~/.bashrc
else
    echo "    Keeping existing bashrc!"
fi
