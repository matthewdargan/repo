#!/usr/bin/bash

# Change this in your own fork if needed
export GITUSER=mpdargan

# First whatever the system has (required for completion, etc.)
if [ -e /etc/bashrc ]; then
    source /etc/bashrc
fi

source "$HOME/.shell.d/detection.sh"
source "$HOME/.shell.d/git.sh"
source "$HOME/.shell.d/path.sh"
source "$HOME/.shell.d/history.sh"
source "$HOME/.shell.d/pager.sh"
source "$HOME/.shell.d/settings.bash"
source "$HOME/.shell.d/prompt.sh"
source "$HOME/.shell.d/editor.sh"
source "$HOME/.shell.d/python.sh"
source "$HOME/.shell.d/dircolors.sh"
source "$HOME/.shell.d/completion.bash"
source "$HOME/.shell.d/colors.sh"
source "$HOME/.shell.d/termcap-colors.sh"
source "$HOME/.shell.d/golang.sh"
source "$HOME/.shell.d/aliases.sh"
source "$HOME/.shell.d/api-keys.sh"
source "$HOME/.shell.d/fzf.sh"
[ -r ~/.bash_personal ] && source ~/.bash_personal
[ -r ~/.bash_private ] && source ~/.bash_private
[ -r ~/.bash_work ] && source ~/.bash_work
