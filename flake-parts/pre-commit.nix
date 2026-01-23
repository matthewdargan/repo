{inputs, ...}: {
  imports = [
    inputs.pre-commit-hooks.flakeModule
  ];

  perSystem = {
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
