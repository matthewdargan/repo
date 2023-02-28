# This file is only here because it is required by some applications.
if [ -n "$BASH_VERSION" ]; then
	# Include .bashrc if it exists
	if [ -f "$HOME/.bashrc" ]; then
		. "$HOME/.bashrc"
	fi
fi
