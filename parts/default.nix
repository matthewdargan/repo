_: {
  perSystem = {
    config,
    inputs',
    pkgs,
    ...
  }: {
    devShells.default = pkgs.mkShell {
      packages = [inputs'.home-manager.packages.home-manager pkgs.age pkgs.nh pkgs.sops pkgs.ssh-to-age];
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
