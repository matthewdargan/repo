{inputs, ...}: {
  imports = [inputs.disko.nixosModules.disko];
  boot = {
    kernel.sysctl = {
      "net.ipv4.conf.all.forwarding" = 1;
      "net.ipv4.conf.br-lan.rp_filter" = 1;
      "net.ipv4.conf.default.rp_filter" = 1;
      "net.ipv4.conf.enp3s0.rp_filter" = 1;
    };
    loader = {
      efi.canTouchEfiVariables = true;
      systemd-boot.enable = true;
    };
    supportedFilesystems = ["ext4"];
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
        device = "/dev/nvme0n1";
        type = "disk";
      };
    };
  };
}
