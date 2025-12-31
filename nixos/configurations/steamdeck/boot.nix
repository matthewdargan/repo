{
  config,
  inputs,
  modulesPath,
  ...
}: {
  imports = [
    inputs.disko.nixosModules.disko
    (modulesPath + "/installer/scan/not-detected.nix")
  ];
  boot = {
    initrd = {
      availableKernelModules = [
        "nvme"
        "sdhci_pci"
        "sd_mod"
        "usbhid"
        "usb_storage"
        "xhci_pci"
      ];
      kernelModules = ["amdgpu"];
    };
    kernelModules = ["kvm-amd"];
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
                swap.swapfile.size = "8G";
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
  hardware = {
    cpu.amd.updateMicrocode = config.hardware.enableRedistributableFirmware;
    graphics = {
      enable = true;
      enable32Bit = true;
    };
  };
  nixpkgs.hostPlatform = "x86_64-linux";
  powerManagement.cpuFreqGovernor = "performance";
}
