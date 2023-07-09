case $- in
*i*) ;; # interactive
*) return ;;
esac

if [[ $- == *i* ]]; then
	bind '"\e[A": history-search-backward'
	bind '"\e[B": history-search-forward'
fi

# Add bash completions for specific commands
source /usr/share/bash-completion/completions/git

function _have() {
	type "$1" &>/dev/null
}

export TERM=xterm-256color
export EDITOR=vi
export VISUAL=vi
export GOPATH="$HOME/go"

export LESS="-FXR"
export LESS_TERMCAP_mb=$'\e[35m' # magenta
export LESS_TERMCAP_md=$'\e[33m' # yellow
export LESS_TERMCAP_me=$''
export LESS_TERMCAP_se=$''
export LESS_TERMCAP_so=$'\e[34m' # blue
export LESS_TERMCAP_ue=$''
export LESS_TERMCAP_us=$'\e[4m' # underline
export GROFF_NO_SGR=1           # Required for termcap colors to work on some terminal emulators

# Pager
if [[ -x /usr/bin/lesspipe.sh ]]; then
	export LESSOPEN="| /usr/bin/lesspipe.sh %s"
	export LESSCLOSE="/usr/bin/lesspipe.sh %s %s"
fi

if _have dircolors; then
	if [[ -r "$HOME/.dircolors" ]]; then
		eval "$(dircolors -b "$HOME/.dircolors")"
	else
		eval "$(dircolors -b)"
	fi
fi

export PATH="$HOME/bin:$GOPATH/bin:$HOME/.cargo/bin:/usr/local/go/bin:$PATH"

export HISTCONTROL=ignoreboth
export HISTSIZE=5000
export HISTFILESIZE=10000

shopt -s checkwinsize
shopt -s expand_aliases
shopt -s globstar
shopt -s dotglob
shopt -s extglob
shopt -s histappend
set -o vi

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

unalias -a
alias ls='ls -h --color=auto'
alias ll='ls -alF'
alias l='ls -CF'
alias diff='diff --color'
alias grep='grep --color --ignore-case'
alias temp='cd $(mktemp -d)'
alias view='vi -R' # which is usually linked to vim
alias clear='printf "\e[H\e[2J"'
alias more='less'

_have vim && alias vi=vim

export NVM_DIR="$HOME/.nvm"
[ -s "$NVM_DIR/nvm.sh" ] && \. "$NVM_DIR/nvm.sh"  # This loads nvm
[ -s "$NVM_DIR/bash_completion" ] && \. "$NVM_DIR/bash_completion"  # This loads nvm bash_completion
