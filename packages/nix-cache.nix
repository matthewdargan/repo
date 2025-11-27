{
  perSystem = {
    lib,
    pkgs,
    ...
  }: let
    commonMeta = {
      description = "Nix binary cache server";
      homepage = "https://github.com/matthewdargan/repo";
      license = lib.licenses.bsd3;
      maintainers = with lib.maintainers; [matthewdargan];
    };

    commonAttrs = {
      pname = "nix-cache";
      version = "0.1.0";
      src = lib.cleanSource ../.;
      meta = commonMeta;

      nativeBuildInputs = with pkgs; [clang pkg-config];
      buildInputs = with pkgs; [nix boost nlohmann_json xz];
    };

    commonFlags = "-I. -D_GNU_SOURCE -g -Wall -Wextra -Wno-unknown-warning-option -Wno-unused-function -Wno-unused-variable -Wno-unused-value -Wno-writable-strings -Wno-missing-field-initializers -Wno-missing-braces -Wno-address-of-temporary";
    releaseFlags = "-O3 ${commonFlags} -DBUILD_DEBUG=0";
    debugFlags = "-O0 ${commonFlags} -fno-omit-frame-pointer -DBUILD_DEBUG=1";

    mkVariant = flags: extraAttrs: let
      buildMode =
        if lib.hasInfix "BUILD_DEBUG=1" flags
        then "debug mode"
        else "release mode";
    in
      pkgs.clangStdenv.mkDerivation (commonAttrs
        // {
          buildPhase = ''
            runHook preBuild
            echo "[${buildMode}]"
            NIX_CFLAGS=$(pkg-config --cflags nix-main nix-cmd nix-expr nix-store nix-flake nix-util nix-fetchers liblzma)
            NIX_LIBS=$(pkg-config --libs nix-main nix-cmd nix-expr nix-store nix-flake nix-util nix-fetchers liblzma)
            clang++ ${flags} $NIX_CFLAGS cmd/nix-cache/main.cpp $NIX_LIBS -lm -o nix-cache
            runHook postBuild
          '';
          installPhase = ''
            runHook preInstall
            install -Dm755 nix-cache $out/bin/nix-cache
            runHook postInstall
          '';
        }
        // extraAttrs);
  in {
    packages = {
      nix-cache = mkVariant releaseFlags {};
      nix-cache-debug = mkVariant debugFlags {dontStrip = true;};
    };
  };
}
