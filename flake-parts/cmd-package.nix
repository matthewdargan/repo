{
  lib,
  pkgs,
}: {
  mkCmdPackage = {
    pname,
    description,
    version,
    binaryName ? pname,
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

    releaseFlags = "-O3 -I. -g -Wall -Wextra -DBUILD_DEBUG=0";
    debugFlags = "-O0 -I. -g -fsanitize=address -fno-omit-frame-pointer -Wall -Wextra -DBUILD_DEBUG=1";

    mkVariant = flags: extraAttrs:
      pkgs.clangStdenv.mkDerivation (commonAttrs
        // {
          buildPhase = ''
            runHook preBuild
            clang ${flags} cmd/${pname}/main.c -o ${binaryName}
            runHook postBuild
          '';
          installPhase = ''
            runHook preInstall
            install -Dm755 ${binaryName} $out/bin/${pname}
            runHook postInstall
          '';
        }
        // extraAttrs);
  in {
    "${pname}" = mkVariant releaseFlags {};
    "${pname}-debug" = mkVariant debugFlags {dontStrip = true;};
  };
}
