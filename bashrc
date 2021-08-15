export TERM=xterm-256color
export GITUSER="${USER}"
export SCRIPTS=~/.local/bin/scripts
test ! -d "$SCRIPTS" && mkdir -p "$SCRIPTS"
export REPOS="${HOME}/repos"
export DOTFILES="${REPOS}/github.com/${GITUSER}/dotfiles"
test -e /etc/bashrc && source /etc/bashrc

case $- in
	*i*) ;;
	*) return ;;
esac

if [[ $- == *i* ]]; then
    bind '"\e[A": history-search-backward'
    bind '"\e[B": history-search-forward'
fi

pathappend() {
    for ARG in "$@"; do
        test -d "${ARG}" || continue
        case ":${PATH}:" in
        *:${ARG}:*) continue ;;
        esac
        export PATH="${PATH:+"${PATH}:"}${ARG}"
    done
}

pathprepend() {
    for ARG in "$@"; do
        test -d "${ARG}" || continue
        case ":${PATH}:" in
        *:${ARG}:*) continue ;;
        esac
        export PATH="${PATH:+"${PATH}:"}${ARG}"
    done
}

cdpathappend() {
    for ARG in "$@"; do
        test -d "${ARG}" || continue
        case ":${CDPATH}:" in
        *:${ARG}:*) continue ;;
        esac
        export CDPATH="${CDPATH:+"${CDPATH}:"}${ARG}"
    done
}

pathprepend \
    "${SCRIPTS}" \
    "${HOME}/.local/bin" \
    "${HOME}/.local/go/bin" \
    "${HOME}/.cargo/bin" \
    "${HOME}/.poetry/bin" \
    "${HOME}/bin" \
    "${PLAN9}/bin"

pathappend \
    "/usr/local/opt/coreutils/libexec/gnubin" \
    "/usr/local/bin" \
	"/usr/local/sbin" \
    "/usr/local/go/bin" \
    "/usr/sbin" \
	"/usr/bin" \
	"/snap/bin" \
	"/sbin" \
	"/bin"

cdpathappend \
    "${REPOS}/github.com/*" \
    "${REPOS}" \
    "${HOME}"

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
set -o noclobber

PROMPT_LONG=80
PROMPT_MAX=95

__ps1() {
    local P='$' # changes to hashtag when root

    # set shortcuts for all the colors
    if test -n "${ZSH_VERSION}"; then
        local r='%F{red}'
        local g='%F{black}'
        local h='%F{blue}'
        local u='%F{yellow}'
        local p='%F{yellow}'
        local w='%F{magenta}'
        local b='%F{cyan}'
        local x='%f'
    else
		local r='\[\e[31m\]'
		local g='\[\e[30m\]'
		local h='\[\e[34m\]'
		local u='\[\e[33m\]'
		local p='\[\e[33m\]'
		local w='\[\e[35m\]'
		local b='\[\e[36m\]'
		local x='\[\e[0m\]'
    fi

    # watch out, you're root
    if test "${EUID}" == 0; then
        P='#'
        if test -n "${ZSH_VERSION}"; then
            u='$F{red}'
        else
            u=$r
        fi
        p=$u
    fi

    local dir="$(basename $PWD)"
    if test "${dir}" = _ ; then
        dir=${PWD#*${PWD%/*/_}}
        dir=${dir#/}
    elif test "${dir}" = work; then
        dir=${PWD#*${PWD%/*/work}}
        dir=${dir#/}
    fi

    local B=$(git branch --show-current 2>/dev/null)
    test "${dir}" = "${B}" && B='.'
    local countme="$USER@$(hostname):$dir($B)\$ "

    test "${B}" = master -o "${B}" = main && b=$r
    test -n "${B}" && B="$g($b$B$g)"

    # let's see how long this thing really is
    if test -n "${ZSH_VERSION}"; then
        local short="$u%n$g@$h%m$g:$w$dir$B$p$P$x "
        local long="$g╔ $u%n$g@%m\h$g:$w$dir$B\n$g╚ $p$P$x "
        local double="$g╔ $u%n$g@%m\h$g:$w$dir\n$g║ $B\n$g╚ $p$P$x "
    else
        local short="$u\u$g@$h\h$g:$w$dir$B$p$P$x "
        local long="$g╔ $u\u$g@$h\h$g:$w$dir$B\n$g╚ $p$P$x "
        local double="$g╔ $u\u$g@$h\h$g:$w$dir\n$g║ $B\n$g╚ $p$P$x "
    fi

    if test ${#countme} -gt "${PROMPT_MAX}";  then
        PS1="${double}"
    elif test ${#countme} -gt "${PROMPT_LONG}";  then
        PS1="${long}"
    else
        PS1="${short}"
    fi
}

PROMPT_COMMAND="__ps1"

export HRULEWIDTH=73
export EDITOR=vi
export VISUAL=vi
export EDITOR_PREFIX=vi

test -d ~/.vim/spell && export VIMSPELL=(~/.vim/spell/*.add)

export FZF_DEFAULT_COMMAND='fd --type f'
export FZF_CTRL_T_COMMAND="$FZF_DEFAULT_COMMAND"
export PYTHONDONTWRITEBYTECODE=1

clear() { printf "\e[H\e[2J"; } && export -f clear
c() { printf "\e[H\e[2J"; } && export -f c

if which dircolors &>/dev/null; then
  	if test -r ~/.dircolors; then
    	eval "$(dircolors -b ~/.dircolors)"
  	else
    	eval "$(dircolors -b)"
  	fi
fi

owncomp=(pdf md auth config ./setup)
for i in ${owncomp[@]}; do complete -C $i $i; done

type gh &>/dev/null && . <(gh completion -s bash)

export GOPRIVATE="github.com/$GITUSER/*,github.com/eBay-Swippy-Swappy-Funtime/*"
export GOPATH="$HOME/.local/go"
export GOBIN="$HOME/.local/go/bin"
export GOPROXY=direct
export CGO_ENABLED=0

unalias -a
alias d=docker
alias grep='grep -i --color=auto'
alias egrep='egrep -i --color=auto'
alias fgrep='fgrep -i --color=auto'
alias curl='curl -L'
alias ls='ls -h --color=auto'
alias ll='ls -alF'
alias la='ls -A'
alias l='ls -CF'
alias '?'=duck
alias '??'=google
alias free='free -h'
alias df='df -h'
alias top=htop
alias dot="cd ${DOTFILES}"
alias scripts="cd ${SCRIPTS}"

which vim &>/dev/null && alias vi=vim

source "${HOME}/.shell.d/api-keys.sh"
test -r ~/.bash_personal && source ~/.bash_personal
test -r ~/.bash_private && source ~/.bash_private
test -r ~/.bash_work && source ~/.bash_work
