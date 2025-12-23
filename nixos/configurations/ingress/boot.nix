{
  inputs,
  modulesPath,
  ...
}: {
  imports = [
    inputs.disko.nixosModules.disko
    (modulesPath + "/profiles/qemu-guest.nix")
  ];
  boot = {
    loader = {
      grub = {
        device = "/dev/sda";
        efiSupport = true;
      };
    };
  };
  disko.devices = {
    disk = {
      main = {
        content = {
          partitions = {
            boot = {
              priority = 1;
              size = "1M";
              type = "EF02";
            };
            ESP = {
              content = {
                format = "vfat";
                mountOptions = ["umask=0077"];
                mountpoint = "/boot";
                type = "filesystem";
              };
              name = "ESP";
              size = "1G";
              type = "EF00";
            };
            root = {
              content = {
                extraArgs = ["-f"];
                mountpoint = "/partition-root";
                subvolumes = {
                  "/home" = {
                    mountOptions = ["compress=zstd"];
                    mountpoint = "/home";
                  };
                  "/nix" = {
                    mountOptions = [
                      "compress=zstd"
                      "noatime"
                    ];
                    mountpoint = "/nix";
                  };
                  "/rootfs".mountpoint = "/";
                };
                swap.swapfile.size = "20M";
                type = "btrfs";
              };
              size = "100%";
            };
          };
          type = "gpt";
        };
        device = "/dev/sda";
        type = "disk";
      };
    };
  };
}
