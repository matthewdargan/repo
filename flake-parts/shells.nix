{
  perSystem = {
    config,
    inputs',
    pkgs,
    ...
  }: {
    devShells.default = pkgs.mkShell {
      packages = [
        inputs'.home-manager.packages.home-manager
        pkgs.gdb
        pkgs.libllvm
        pkgs.nh
        pkgs.valgrind
      ];
      shellHook = "${config.pre-commit.installationScript}";
    };
  };
}
