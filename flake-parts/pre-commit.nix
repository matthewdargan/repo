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
          end-of-file-fixer.enable = true;
          mixed-line-endings.enable = true;
          statix.enable = true;
          trim-trailing-whitespace.enable = true;
        };
        src = ../.;
      };
    };
  };
}
