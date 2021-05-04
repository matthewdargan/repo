if [[ $- == *i* ]]; then
    bind '"\e[A": history-search-backward'
    bind '"\e[B": history-search-forward'
fi

complete -C pdf pdf
complete -C md md
complete -C auth auth
complete -C config config
complete -C ./setup ./setup

if type gh &>/dev/null; then
    eval "$(gh completion -s bash)"
fi

if type docker &>/dev/null; then
    complete -F _docker d
fi
