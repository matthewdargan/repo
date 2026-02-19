{
  lib,
  pkgs,
}: {
  mkCmdPackage = {
    pname,
    description,
    version,
    binaryName ? pname,
    buildInputs ? [],
    nativeBuildInputs ? [],
    extraLinkFlags ? "",
    postPatch ? "",
    postInstall ? "",
  }: let
    commonMeta = {
      inherit description;
      homepage = "https://github.com/matthewdargan/repo";
      license = lib.licenses.bsd3;
      maintainers = with lib.maintainers; [matthewdargan];
    };

    commonAttrs = {
      inherit pname version;
      src = lib.cleanSource ../.;
      meta = commonMeta;
    };

    commonFlags = "-I. -D_GNU_SOURCE -g -Wall -Wextra -Wno-unknown-warning-option -Wno-unused-function -Wno-unused-variable -Wno-unused-value";
    releaseFlags = "-O3 ${commonFlags} -DBUILD_DEBUG=0";
    debugFlags = "-O0 ${commonFlags} -fsanitize=address -fno-omit-frame-pointer -DBUILD_DEBUG=1";

    mkVariant = flags: extraAttrs: let
      buildMode =
        if lib.hasInfix "BUILD_DEBUG=1" flags
        then "debug mode"
        else "release mode";
    in
      pkgs.clangStdenv.mkDerivation (commonAttrs
        // {
          inherit buildInputs nativeBuildInputs postPatch;
          buildPhase = ''
            runHook preBuild
            echo "[${buildMode}]"
            clang ${flags} \
              ${lib.strings.concatMapStringsSep " " (p: "-I${p}/include") buildInputs} \
              cmd/${pname}/main.c -o ${binaryName} \
              ${lib.strings.concatMapStringsSep " " (p: "-L${p}/lib") buildInputs} \
              ${extraLinkFlags}
            runHook postBuild
          '';
          installPhase = ''
            runHook preInstall
            install -Dm755 ${binaryName} $out/bin/${pname}
            runHook postInstall
          '';
          inherit postInstall;
        }
        // extraAttrs);
  in {
    "${pname}" = mkVariant releaseFlags {};
    "${pname}-debug" = mkVariant debugFlags {dontStrip = true;};
  };
}
