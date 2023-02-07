export EDITOR='vim'
export VISUAL='vim'
export GOPATH="$HOME/go"
export PATH="$HOME/bin:$GOPATH/bin:$HOME/.cargo/bin:/usr/local/go/bin:$PATH"
export ZSH="$HOME/.oh-my-zsh"

ZSH_THEME="gitster"
plugins=(
    zsh-autosuggestions
    zsh-syntax-highlighting
)

source "$ZSH/oh-my-zsh.sh"
bindkey -v

alias ls='ls -h --color=auto'
alias ll='ls -alF'
alias la='ls -A'
alias l='ls -CF'
