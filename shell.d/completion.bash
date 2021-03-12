if [ -r /usr/share/bash-completion/bash_completion ]; then
	source /usr/share/bash-completion/bash_completion
fi

if [[ $- == *i* ]]; then
    bind '"\e[A": history-search-backward'
    bind '"\e[B": history-search-forward'
fi
