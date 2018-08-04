#!/usr/bin/env bash

cd "$(dirname "${BASH_SOURCE}")";

echo "Shell installation script for odarriba's dotfiles";
echo "-------------------------------------------------";
echo "";

installSoftware() {
	# Install zsh and required software
	echo "[INFO] Installing required software (zsh, git, curl, wget and python-pip)...";
    sudo apt-get install -y vim zsh git-core curl wget python3 python-pip unzip fonts-hack-ttf 

	# Install Oh My Zsh!
	echo "[INFO] Installing Oh My Zsh...";
    sh -c "$(curl -fsSL https://raw.githubusercontent.com/robbyrussell/oh-my-zsh/master/tools/install.sh)"
	
    # Change the shell to zsh
	chsh -s $(which zsh)

    # Install Anaconda
    cd /tmp
    curl -O https://repo.anaconda.com/archive/Anaconda3-5.1.0-Linux-x86_64.sh
    chmod +x Anaconda3-5.1.0-Linux-x86_64.sh
    sudo ./Anaconda3-5.1.0-Linux-x86_64.sh -b -p
    conda install ipython
    rm -f Anaconda3-5.1.0-Linux-x86_64.sh
    cd
}

updateApt() {
	echo "[INFO] Updating APT repositories...";
	sudo apt-get update && sudo apt-get -y upgrade;
}

syncConfig() {
	echo "[INFO] Syncing configuration...";
	# Avoid copying gnupg config for OSX on Linux
	rsync --exclude ".git/" --exclude ".DS_Store" --exclude ".gitignore" --exclude "install.sh" \
	--exclude "README.md" --exclude "LICENSE" -avh --no-perms . ~;
    rm -f ~/.config/Code/User/settings.json
    cp settings.json ~/.config/Code/User/
}

doIt() {
	updateApt;
	installSoftware;
	syncConfig;
}

if [ "$1" == "--force" -o "$1" == "-f" ]; then
	doIt;
else
	read -p "I'm about to change the configuration files placed in your home directory. Do you want to continue? (y/n) " -n 1;
	echo "";
	if [[ $REPLY =~ ^[Yy]$ ]]; then
		doIt;
	fi;
fi;

echo "";
echo "[INFO] If there isn't any error message, the process is completed.";