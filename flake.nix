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
          packages = [
            pkgs.bear
            pkgs.clang
            pkgs.gdb
            pkgs.libllvm
            pkgs.valgrind
          ];
          shellHook = "${config.pre-commit.installationScript}";
        };
        packages = {
          "9bind" = pkgs.clangStdenv.mkDerivation {
            buildPhase = "./build release 9bind";
            installPhase = ''
              mkdir -p "$out/bin"
              cp ./bin/9bind "$out/bin"
            '';
            meta = with lib; {
              description = "Performs a bind mount";
              homepage = "https://github.com/matthewdargan/src";
              license = licenses.bsd3;
              maintainers = with maintainers; [matthewdargan];
            };
            pname = "9bind";
            src = ./.;
            version = "0.1.0";
          };
          "9mount" = pkgs.clangStdenv.mkDerivation {
            buildPhase = "./build release 9mount";
            installPhase = ''
              mkdir -p "$out/bin"
              cp ./bin/9mount "$out/bin"
            '';
            meta = with lib; {
              description = "Mounts a 9p filesystem";
              homepage = "https://github.com/matthewdargan/src";
              license = licenses.bsd3;
              maintainers = with maintainers; [matthewdargan];
            };
            pname = "9mount";
            src = ./.;
            version = "0.1.0";
          };
          "9umount" = pkgs.clangStdenv.mkDerivation {
            buildPhase = "./build release 9umount";
            installPhase = ''
              mkdir -p "$out/bin"
              cp ./bin/9umount "$out/bin"
            '';
            meta = with lib; {
              description = "Unmounts a 9p filesystem";
              homepage = "https://github.com/matthewdargan/src";
              license = licenses.bsd3;
              maintainers = with maintainers; [matthewdargan];
            };
            pname = "9umount";
            src = ./.;
            version = "0.1.0";
          };
          mediasrv = pkgs.clangStdenv.mkDerivation {
            buildInputs = [pkgs.ffmpeg];
            buildPhase = "./build release mediasrv";
            installPhase = ''
              mkdir -p "$out/bin"
              cp ./bin/mediasrv "$out/bin"
            '';
            meta = with lib; {
              description = "Media server";
              homepage = "https://github.com/matthewdargan/src";
              license = licenses.bsd3;
              maintainers = with maintainers; [matthewdargan];
            };
            pname = "mediasrv";
            src = ./.;
            version = "0.10.0";
          };
          tor = pkgs.clangStdenv.mkDerivation {
            buildInputs = [
              pkgs.curl
              pkgs.libxml2
              pkgs.pkg-config
            ];
            buildPhase = "./build release tor";
            installPhase = ''
              mkdir -p "$out/bin"
              cp ./bin/tor "$out/bin"
            '';
            meta = with lib; {
              description = "Queries for torrents";
              homepage = "https://github.com/matthewdargan/src";
              license = licenses.bsd3;
              maintainers = with maintainers; [matthewdargan];
            };
            nativeBuildInputs = [pkgs.pkg-config];
            pname = "tor";
            src = ./.;
            version = "0.1.1";
          };
          tordl = pkgs.clangStdenv.mkDerivation {
            buildInputs = [
              pkgs.boost
              pkgs.libtorrent-rasterbar
              pkgs.pkg-config
            ];
            buildPhase = "./build release tordl";
            installPhase = ''
              mkdir -p "$out/bin"
              cp ./bin/tordl "$out/bin"
            '';
            meta = with lib; {
              description = "Downloads provided torrents";
              homepage = "https://github.com/matthewdargan/src";
              license = licenses.bsd3;
              maintainers = with maintainers; [matthewdargan];
            };
            nativeBuildInputs = [pkgs.pkg-config];
            pname = "tordl";
            src = ./.;
            version = "0.1.1";
          };
          torrss = pkgs.clangStdenv.mkDerivation {
            buildInputs = [
              pkgs.boost
              pkgs.curl
              pkgs.libtorrent-rasterbar
              pkgs.libxml2
              pkgs.pkg-config
            ];
            buildPhase = "./build release torrss";
            installPhase = ''
              mkdir -p "$out/bin"
              cp ./bin/torrss "$out/bin"
            '';
            meta = with lib; {
              description = "Queries RSS feeds and downloads torrents automatically";
              homepage = "https://github.com/matthewdargan/src";
              license = licenses.bsd3;
              maintainers = with maintainers; [matthewdargan];
            };
            nativeBuildInputs = [pkgs.pkg-config];
            pname = "torrss";
            src = ./.;
            version = "0.1.1";
          };
        };
        pre-commit = {
          check.enable = false;
          settings = {
            hooks = {
              alejandra.enable = true;
              clang-format = {
                enable = false;
                types_or = lib.mkForce [
                  "c"
                  "c++"
                ];
              };
              deadnix.enable = true;
              statix.enable = true;
            };
            src = ./.;
          };
        };
      };
      systems = [
        "aarch64-linux"
        "x86_64-linux"
      ];
    };
}
