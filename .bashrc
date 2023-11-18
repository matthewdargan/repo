case $- in
	*i*) ;; # interactive
	*) return ;;
esac

if [[ $- == *i* ]]; then
	bind '"\e[A": history-search-backward'
	bind '"\e[B": history-search-forward'
fi

if [[ $(uname) == "Darwin" ]]; then
	source /opt/homebrew/etc/bash_completion.d/git-completion.bash
	eval "$(/opt/homebrew/bin/brew shellenv)"
else
	source /usr/share/bash-completion/completions/git
fi

export HISTCONTROL=ignoreboth
export HISTSIZE=5000
export HISTFILESIZE=10000
export GOPATH="$HOME/go"
export PATH="$HOME/bin:$GOPATH/bin:$PATH:/usr/local/go/bin"
export EDITOR=vim
export VISUAL=vim

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
alias l='ls -CF'

export NVM_DIR="$HOME/.nvm"
[ -s "$NVM_DIR/nvm.sh" ] && \. "$NVM_DIR/nvm.sh"  # This loads nvm
[ -s "$NVM_DIR/bash_completion" ] && \. "$NVM_DIR/bash_completion"  # This loads nvm bash_completion
