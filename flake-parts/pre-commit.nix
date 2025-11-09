{inputs, ...}: {
  imports = [
    inputs.pre-commit-hooks.flakeModule
  ];

  perSystem = {lib, ...}: {
    pre-commit = {
      settings = {
        hooks = {
          alejandra.enable = true;
          clang-format = {
            enable = true;
            types_or = lib.mkForce [
              "c"
              "c++"
            ];
          };
          deadnix.enable = true;
          statix.enable = true;
        };
        src = ../.;
      };
    };
  };
}
