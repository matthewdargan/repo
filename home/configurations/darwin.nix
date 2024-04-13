inputs: _: {
  home.homeDirectory = "/Users/mpd";
  nixpkgs.overlays = [inputs.nixpkgs-firefox-darwin.overlay];
}
