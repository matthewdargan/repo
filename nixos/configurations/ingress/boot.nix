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
      efi.canTouchEfiVariables = true;
      systemd-boot.enable = true;
    };
  };
  disko.devices = {
    disk = {
      main = {
        content = {
          partitions = {
            ESP = {
              content = {
                format = "vfat";
                mountOptions = ["umask=0077"];
                mountpoint = "/boot";
                type = "filesystem";
              };
              end = "1G";
              name = "ESP";
              priority = 1;
              start = "1M";
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
