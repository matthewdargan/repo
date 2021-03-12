unalias -a

alias grep='grep -i --colour=auto'
alias egrep='egrep -i --colour=auto'
alias fgrep='fgrep -i --colour=auto'
alias curl='curl -L'
alias ls='ls -h --color=auto'
alias ll='ls -alF'
alias la='ls -A'
alias l='ls -CF'
alias free='free -h'
alias df='df -h'
alias top='htop'
alias ga='git add .'
alias gcm='git commit -m'
alias gp='git push'
alias gl='git log'
alias gs='git status'
alias gr='git reset --hard HEAD'

which nvim &>/dev/null && alias vi=nvim
which nvim &>/dev/null && alias vim=nvim
