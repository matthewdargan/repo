export HRULEWIDTH=73
alias vi="vim"
export EDITOR=vim
export VISUAL=vim
export EDITOR_PREFIX=vim

if [ -d ~/.vim/spell ]; then
	export VIMSPELL=(~/.vim/spell/*.add)
fi

if [ -d ~/.vimpersonal ]; then
	personalspell=(~/.vimpersonal/spell/*.add)
  	[ -n "$personalspell" ] && VIMSPELL=$personalspell
fi

if [ -d ~/.vimprivate ]; then
  	privatespell=(~/.vimprivate/spell/*.add)
  	[ -n $privatespell ] && VIMSPELL=$privatespell
fi
