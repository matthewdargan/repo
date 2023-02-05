export EDITOR='vim'
export VISUAL='vim'
export PLAN9='/usr/local/plan9'
export GOPATH="$HOME/go"
export PATH="$HOME/bin:$HOME/.cargo/bin:/usr/local/go/bin:$PATH:$PLAN9/bin:$GOPATH/bin"

alias ls='ls -h --color=auto'
alias ll='ls -alF'
alias la='ls -A'
alias l='ls -CF'

if [ -f /etc/bashrc ]; then
	. /etc/bashrc
elif [ -f /etc/bash.bashrc ]; then
	. /etc/bash.bashrc
fi
PS1='\w:$(git branch 2>/dev/null | grep '"'"'^*'"'"' | colrm 1 2)$ '
