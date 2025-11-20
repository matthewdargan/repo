{
  perSystem = {
    lib,
    pkgs,
    ...
  }: let
    cmdPackage = import ../flake-parts/cmd-package.nix {inherit lib pkgs;};
    basePackages = cmdPackage.mkCmdPackage {
      pname = "mount-9p";
      description = "mount(8) helper for 9P filesystems";
      version = "0.1.0";
    };
    # Wrap to install as mount.9p (what mount(8) expects)
    wrapPackage = pkg:
      pkgs.runCommand "mount-9p-wrapped" {} ''
        mkdir -p $out/bin
        cp ${pkg}/bin/mount-9p $out/bin/mount.9p
      '';
  in {
    packages = {
      mount-9p = wrapPackage basePackages.mount-9p;
      mount-9p-debug = wrapPackage basePackages.mount-9p-debug;
    };
  };
}
