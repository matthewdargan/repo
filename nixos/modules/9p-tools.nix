{
  pkgs,
  self,
  ...
}: {
  environment.systemPackages = [
    self.packages.${pkgs.stdenv.hostPlatform.system}.mount-9p
  ];
  security.wrappers = {
    "9bind" = {
      owner = "root";
      group = "root";
      permissions = "u+rx,g+x,o+x";
      setuid = true;
      source = "${self.packages.${pkgs.stdenv.hostPlatform.system}."9bind"}/bin/9bind";
    };
    "9mount" = {
      owner = "root";
      group = "root";
      permissions = "u+rx,g+x,o+x";
      setuid = true;
      source = "${self.packages.${pkgs.stdenv.hostPlatform.system}."9mount"}/bin/9mount";
    };
  };
}
