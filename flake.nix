{
  description = "home config";
  inputs = {
    firefox-addons = {
      inputs.nixpkgs.follows = "nixpkgs";
      url = "gitlab:rycee/nur-expressions?dir=pkgs/firefox-addons";
    };
    ghlink = {
      inputs.nixpkgs.follows = "nixpkgs";
      url = "github:matthewdargan/ghlink";
    };
    home-manager = {
      inputs.nixpkgs.follows = "nixpkgs";
      url = "github:nix-community/home-manager";
    };
    manga-alert = {
      inputs.nixpkgs.follows = "nixpkgs";
      url = "github:matthewdargan/manga-alert";
    };
    nix-go = {
      inputs.nixpkgs.follows = "nixpkgs";
      url = "github:matthewdargan/nix-go";
    };
    nixpkgs.url = "nixpkgs/nixos-unstable";
    parts.url = "github:hercules-ci/flake-parts";
    pre-commit-hooks = {
      inputs.nixpkgs.follows = "nixpkgs";
      url = "github:cachix/pre-commit-hooks.nix";
    };
  };
  outputs = {parts, ...} @ inputs:
    parts.lib.mkFlake {inherit inputs;} {
      imports = [
        ./home/configurations
        inputs.pre-commit-hooks.flakeModule
        ./nixos/configurations
        ./parts
      ];
      systems = ["aarch64-darwin" "x86_64-linux"];
    };
}
