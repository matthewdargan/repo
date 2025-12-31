{
  config,
  modulesPath,
  ...
}: {
  imports = [(modulesPath + "/installer/scan/not-detected.nix")];
  boot = {
    initrd.availableKernelModules = [
      "ahci"
      "nvme"
      "sd_mod"
      "thunderbolt"
      "usbhid"
      "usb_storage"
      "xhci_pci"
    ];
    kernelModules = ["kvm-amd"];
  };
  fileSystems = {
    "/" = {
      device = "/dev/disk/by-uuid/a030e8db-e564-4282-a008-15cf1992b4ab";
      fsType = "ext4";
    };
    "/boot" = {
      device = "/dev/disk/by-uuid/0CB3-CDA7";
      fsType = "vfat";
      options = ["dmask=0077" "fmask=0077"];
    };
    "/media" = {
      device = "/dev/sda";
      fsType = "btrfs";
      options = ["compress=zstd" "subvol=media"];
    };
  };
  hardware.cpu.amd.updateMicrocode = config.hardware.enableRedistributableFirmware;
  nixpkgs.hostPlatform = "x86_64-linux";
  powerManagement.cpuFreqGovernor = "performance";
  swapDevices = [
    {device = "/dev/disk/by-uuid/1cf7520a-1f19-4c4d-9eb0-cd43e22c789a";}
  ];
}
