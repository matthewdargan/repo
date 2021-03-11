# This file is necessary since some applications require it.
if [ -n "$BASH_VERSION" ]; then
    # Include .bashrc if it exists
    if [ -f "$HOME/.bashrc" ]; then
        . "$HOME/.bashrc"
        ps1min
    fi
fi

export PATH="$HOME/.cargo/bin:$PATH"
