{
  description = "monorepo";
  inputs = {
    disko = {
      inputs.nixpkgs.follows = "nixpkgs";
      url = "github:nix-community/disko";
    };
    firefox-addons = {
      inputs.nixpkgs.follows = "nixpkgs";
      url = "gitlab:rycee/nur-expressions?dir=pkgs/firefox-addons";
    };
    ghostty = {
      inputs.nixpkgs.follows = "nixpkgs";
      url = "github:ghostty-org/ghostty";
    };
    home-manager = {
      inputs.nixpkgs.follows = "nixpkgs";
      url = "github:nix-community/home-manager";
    };
    nixpkgs.url = "nixpkgs/nixos-unstable";
    nixvim = {
      inputs.nixpkgs.follows = "nixpkgs";
      url = "github:nix-community/nixvim";
    };
    parts.url = "github:hercules-ci/flake-parts";
    pre-commit-hooks = {
      inputs.nixpkgs.follows = "nixpkgs";
      url = "github:cachix/pre-commit-hooks.nix";
    };
    u9fs = {
      inputs.nixpkgs.follows = "nixpkgs";
      url = "github:justinrubek/u9fs";
    };
  };
  outputs = {parts, ...} @ inputs:
    parts.lib.mkFlake {inherit inputs;} {
      imports = [
        ./home/configurations
        inputs.pre-commit-hooks.flakeModule
        ./nixos/configurations
        ./packages
      ];
      perSystem = {
        config,
        inputs',
        lib,
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
      systems = ["x86_64-linux"];
    };
}
