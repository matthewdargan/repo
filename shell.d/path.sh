export SCRIPTS=$REPOS/github.com/$GITUSER/dotfiles/scripts

export PATH=\
$SCRIPTS:\
$HOME/.local/bin:\
$HOME/.local/go/bin:\
$HOME/.cargo/bin:\
$HOME/.poetry/bin:\
/usr/local/go/bin:\
/usr/local/bin:\
/usr/local/sbin:\
/usr/sbin:\
/usr/bin:\
/snap/bin:\
/sbin:\
/bin

# Be sure not to remove ./ or stuff gets weird.
export CDPATH=\
./:\
$REPOS/github.com/$GITUSER:\
$REPOS/github.com/*:\
$REPOS/github.com:\
$REPOS:\
$HOME
