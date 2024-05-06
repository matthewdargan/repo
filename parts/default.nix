_: {
  perSystem = {
    config,
    inputs',
    pkgs,
    ...
  }: {
    devShells.default = pkgs.mkShell {
      packages = [inputs'.home-manager.packages.home-manager pkgs.nh];
      shellHook = "${config.pre-commit.installationScript}";
    };
    pre-commit = {
      settings = {
        hooks = {
          alejandra.enable = true;
          deadnix.enable = true;
          statix.enable = true;
        };
        src = ../.;
      };
    };
  };
}
