if [[ $- == *i* ]]; then
  bind '"\e[A": history-search-backward'
  bind '"\e[B": history-search-forward'
fi

if [[ "$(uname)" == 'Darwin' && -x '/opt/homebrew/bin/brew' ]]; then
  eval "$(/opt/homebrew/bin/brew shellenv)"
  git_completion_path='/opt/homebrew/etc/bash_completion.d/git-completion.bash'
  aws_completer_path='/opt/homebrew/bin/aws_completer'
else
  git_completion_path='/usr/share/bash-completion/completions/git'
  aws_completer_path='/usr/local/bin/aws_completer'
fi

[[ -r "$git_completion_path" ]] && source "$git_completion_path"
[[ -r "$aws_completer_path" ]] && complete -C "$aws_completer_path" aws

export HISTCONTROL=ignoreboth
export GOPATH="$HOME/go"
export PATH="$HOME/bin:$GOPATH/bin:$PATH:/usr/local/go/bin"
export EDITOR=nvim
export VISUAL=nvim

function __ps1() {
  local P='$' dir="${PWD##*/}" B \
    r='\[\e[31m\]' g='\[\e[1;30m\]' h='\[\e[34m\]' \
    u='\[\e[33m\]' p='\[\e[34m\]' w='\[\e[35m\]' \
    b='\[\e[36m\]' x='\[\e[0m\]'

  [[ $EUID == 0 ]] && P='#' && u=$r && p=$u # Root
  [[ $PWD = / ]] && dir=/
  [[ $PWD = "$HOME" ]] && dir='~'

  B=$(git branch --show-current 2>/dev/null)
  [[ $dir = "$B" ]] && B=.
  [[ $B == master || $B == main ]] && b="$r"
  [[ -n "$B" ]] && B="$g($b$B$g)"
  PS1="$u\u$g@$h\h$g:$w$dir$B$p$P$x "
}

PROMPT_COMMAND="__ps1"
set -o vi

alias ls='ls -h --color=auto'
alias ll='ls -alF'
alias vim=nvim

