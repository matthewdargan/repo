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
    jovian = {
      inputs.nixpkgs.follows = "nixpkgs";
      url = "github:jovian-experiments/jovian-nixos";
    };
    nixpkgs.url = "nixpkgs/nixos-unstable";
    nixvim = {
      inputs.nixpkgs.follows = "nixpkgs";
      url = "github:nix-community/nixvim";
    };
    flake-parts.url = "github:hercules-ci/flake-parts";
    pre-commit-hooks = {
      inputs.nixpkgs.follows = "nixpkgs";
      url = "github:cachix/pre-commit-hooks.nix";
    };
  };
  outputs = {flake-parts, ...} @ inputs:
    flake-parts.lib.mkFlake {inherit inputs;} {
      systems = ["x86_64-linux"];
      imports = [
        ./flake-parts/checks.nix
        ./flake-parts/pre-commit.nix
        ./flake-parts/shells.nix
        ./home/configurations
        ./home/modules
        ./nixos/configurations
        ./nixos/modules
        ./packages
      ];
    };
}
