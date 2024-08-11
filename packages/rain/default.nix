_: {
  perSystem = {
    inputs',
    lib,
    pkgs,
    ...
  }: let
    inherit (inputs'.nix-go.legacyPackages) buildGoModule;
  in {
    packages.rain = buildGoModule {
      meta = with lib; {
        description = "🌧 BitTorrent client and library in Go";
        homepage = "https://github.com/cenkalti/rain";
        license = licenses.mit;
        maintainers = with maintainers; [cenkalti];
      };
      pname = "rain";
      src = pkgs.fetchFromGitHub {
        owner = "cenkalti";
        repo = "rain";
        rev = "v1.12.17";
        sha256 = "sha256-ZO48hY1GnZiSN1LyQ821B1CDmRZJF2AFHBRrHbu07IQ=";
      };
      vendorHash = "sha256-NS/neEyFKYiK7BvYaDXqzNWkIDBw/p9FiAE3qDge6DU=";
      version = "v1.12.17";
    };
  };
}
