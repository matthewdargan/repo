{
  inputs = {
    nixpkgs.url = "nixpkgs/nixos-unstable";
    parts.url = "github:hercules-ci/flake-parts";
    pre-commit-hooks = {
      inputs.nixpkgs.follows = "nixpkgs";
      url = "github:cachix/pre-commit-hooks.nix";
    };
  };
  outputs = inputs:
    inputs.parts.lib.mkFlake {inherit inputs;} {
      imports = [inputs.pre-commit-hooks.flakeModule];
      perSystem = {
        config,
        lib,
        pkgs,
        ...
      }: {
        devShells.default = pkgs.mkShell {
          buildInputs = [
            pkgs.boost
            pkgs.curl
            pkgs.ffmpeg-full
            pkgs.libtorrent-rasterbar
            pkgs.libxml2
            pkgs.pkg-config
          ];
          packages = [pkgs.clang pkgs.gdb pkgs.libllvm pkgs.valgrind];
          shellHook = "${config.pre-commit.installationScript}";
        };
        packages = {
          media = pkgs.clangStdenv.mkDerivation {
            buildInputs = [pkgs.ffmpeg];
            buildPhase = "./build.sh release media";
            installPhase = ''
              mkdir -p "$out/bin"
              cp ./build/media "$out/bin"
            '';
            meta = with lib; {
              description = "Media server";
              homepage = "https://github.com/matthewdargan/media-server";
              license = licenses.bsd3;
              maintainers = with maintainers; [matthewdargan];
            };
            pname = "media";
            src = ./.;
            version = "0.1.0";
          };
          mooch = pkgs.clangStdenv.mkDerivation {
            buildInputs = [
              pkgs.boost
              pkgs.curl
              pkgs.libtorrent-rasterbar
              pkgs.libxml2
              pkgs.pkg-config
            ];
            buildPhase = "./build.sh release mooch";
            installPhase = ''
              mkdir -p "$out/bin"
              cp ./build/mooch "$out/bin"
            '';
            meta = with lib; {
              description = "Queries and downloads torrents";
              homepage = "https://github.com/matthewdargan/media-server";
              license = licenses.bsd3;
              maintainers = with maintainers; [matthewdargan];
            };
            nativeBuildInputs = [pkgs.pkg-config];
            pname = "mooch";
            src = ./.;
            version = "0.6.1";
          };
          moochrss = pkgs.clangStdenv.mkDerivation {
            buildInputs = [
              pkgs.boost
              pkgs.curl
              pkgs.libtorrent-rasterbar
              pkgs.libxml2
              pkgs.pkg-config
            ];
            buildPhase = "./build.sh release moochrss";
            installPhase = ''
              mkdir -p "$out/bin"
              cp ./build/moochrss "$out/bin"
            '';
            meta = with lib; {
              description = "Queries RSS feeds and downloads torrents automatically";
              homepage = "https://github.com/matthewdargan/media-server";
              license = licenses.bsd3;
              maintainers = with maintainers; [matthewdargan];
            };
            nativeBuildInputs = [pkgs.pkg-config];
            pname = "moochrss";
            src = ./.;
            version = "0.3.2";
          };
        };
        pre-commit = {
          check.enable = false;
          settings = {
            hooks = {
              alejandra.enable = true;
              clang-format = {
                enable = false;
                types_or = lib.mkForce ["c" "c++"];
              };
              deadnix.enable = true;
              statix.enable = true;
            };
            src = ./.;
          };
        };
      };
      systems = ["aarch64-linux" "x86_64-linux"];
    };
}
