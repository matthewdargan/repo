{
  pkgs,
  self,
  ...
}: {
  security.wrappers = {
    "9bind" = {
      owner = "root";
      group = "root";
      permissions = "u+rx,g+x,o+x";
      setuid = true;
      source = "${self.packages.${pkgs.system}."9bind"}/bin/9bind";
    };
    "9mount" = {
      owner = "root";
      group = "root";
      permissions = "u+rx,g+x,o+x";
      setuid = true;
      source = "${self.packages.${pkgs.system}."9mount"}/bin/9mount";
    };
    "9umount" = {
      owner = "root";
      group = "root";
      permissions = "u+rx,g+x,o+x";
      setuid = true;
      source = "${self.packages.${pkgs.system}."9umount"}/bin/9umount";
    };
  };
}
