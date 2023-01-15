export PLAN9="/usr/local/plan9"
export GOPATH="$HOME/go"
path=(
    "$HOME/.cargo/bin"
    "$HOME/bin"
    "/usr/local/bin"
    "/usr/local/sbin"
    "/usr/local/go/bin"
    "/usr/bin"
    "/usr/sbin"
    "/bin"
    "/sbin"
    "$PLAN9/bin"
    "$GOPATH/bin"
    "$path"
)
export PATH="$PATH"

# Path to your oh-my-zsh installation.
export ZSH="$HOME/.oh-my-zsh"

ZSH_THEME="gitster"

# Standard plugins can be found in $ZSH/plugins/
# Custom plugins may be added to $ZSH_CUSTOM/plugins/
plugins=(
    autoupdate
    aws
    docker
    docker-compose
    golang
    zsh-autosuggestions
    zsh-syntax-highlighting
)

source $ZSH/oh-my-zsh.sh

# User configuration
export EDITOR="vim"
export VISUAL="vim"
bindkey -v

# Private GitHub organizations
export GOPRIVATE="github.com/eBay-Swippy-Swappy-Funtime"

# Private API keys
if [[ -f "$HOME/.api_keys" ]]; then
    source "$HOME/.api_keys"
fi

alias ls='ls -h --color=auto'
alias ll='ls -alF'
alias la='ls -A'
alias l='ls -CF'

export NVM_DIR="$HOME/.nvm"
[ -s "$NVM_DIR/nvm.sh" ] && \. "$NVM_DIR/nvm.sh"  # This loads nvm
[ -s "$NVM_DIR/bash_completion" ] && \. "$NVM_DIR/bash_completion"  # This loads nvm bash_completion
