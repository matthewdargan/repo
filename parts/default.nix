{...}: {
  perSystem = {
    inputs',
    pkgs,
    ...
  }: {
    devShells.default = pkgs.mkShell {
      packages = [inputs'.home-manager.packages.home-manager];
    };
  };
}
