# My personal dotfiles
## What's included
- gitconfig
- zshrc
- vscode configuration
- Installation script

## Installation Guide
Run these commands in the terminal:
```
cd ~/
git clone https://github.com/matthewdargan/dotfiles.git ~/dotfiles
cd ~/dotfiles
./install.sh
```
This will rsync the files in `dotfiles` to their appropriate locations and install the dependencies needed to use the previously mentioned plugins.
Everything is configured and tweaked within `dotfiles`.
Also, change `.gitconfig` otherwise you will be committing as me.
