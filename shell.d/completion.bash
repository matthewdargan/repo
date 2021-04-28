if [[ $- == *i* ]]; then
    bind '"\e[A": history-search-backward'
    bind '"\e[B": history-search-forward'
fi

complete -C pdf pdf
complete -C md md

if type gh &>/dev/null; then
    eval "$(gh completion -s bash)"
fi

if type docker &>/dev/null; then
    complete -F _docker d
fi
