{
  perSystem = {pkgs, ...}: {
    packages.www = pkgs.stdenv.mkDerivation {
      pname = "www";
      version = "0.1.0";
      src = pkgs.lib.cleanSource ../www;
      dontBuild = true;
      installPhase = ''
        mkdir -p $out
        cp -r * $out/
      '';
    };
  };
}
