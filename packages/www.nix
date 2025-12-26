{
  perSystem = {pkgs, ...}: {
    packages.www = pkgs.stdenv.mkDerivation {
      pname = "www";
      version = "0.1.0";
      src = ../www;
      installPhase = ''
        mkdir -p $out
        cp -r $src/* $out/
      '';
    };
  };
}
