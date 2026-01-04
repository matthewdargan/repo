{
  perSystem = {
    config,
    inputs',
    pkgs,
    ...
  }: {
    devShells.default = pkgs.mkShell {
      packages = [
        config.packages.compile-commands
        inputs'.home-manager.packages.home-manager
        pkgs.clang-tools
        pkgs.gdb
        pkgs.libllvm
        pkgs.nh
        pkgs.valgrind
      ];
      shellHook = ''
        ${config.pre-commit.installationScript}
        if [ ! -f compile_commands.json ] || [ compile_commands.json -ot flake-parts/cmd-package.nix ]; then
          echo "Generating compile_commands.json..."
          generate-compile-commands "$(pwd)"
        fi
      '';
    };
  };
}
