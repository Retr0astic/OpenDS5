{
  self,
  version,
}: {
  config,
  lib,
  pkgs,
  ...
}: let
  cfg = config.services.opends5;

  system = pkgs.stdenv.hostPlatform.system;
  vdsUsers = cfg.users;
  effectiveVdsPackage =
    if cfg.vdsPackage != null then cfg.vdsPackage
    else self.packages.${system}.vds;
in {
  options.services.opends5 = {
    enable =
      lib.mkEnableOption "OpenDS5 DualSense companion and virtual DualSense stack";

    package = lib.mkOption {
      type = lib.types.package;
      default = self.packages.${system}.opends5;
      defaultText =
        lib.literalExpression "opends5.packages.\${pkgs.system}.opends5";
      description = "The OpenDS5 companion application package.";
    };

    vdsPackage = lib.mkOption {
      type = lib.types.nullOr lib.types.package;
      default = null;
      defaultText =
        lib.literalExpression "null (use the flake's standalone vds package)";
      description = ''
        Optional package containing vdsd and vdsctl. An explicit value wins.
        When unset, the standalone flake vds package supplies the daemon,
        udev rules, and WirePlumber configuration independently of the GUI
        companion package.
      '';
    };

    maxPorts = lib.mkOption {
      type = lib.types.ints.between 1 4;
      default = 4;
      description = "Number of virtual DualSense ports to create.";
    };

    users = lib.mkOption {
      type = lib.types.listOf lib.types.str;
      default = [ ];
      example = [ "alice" ];
      description = "Users granted access to the vDS socket through the vds group.";
    };

    disableBluetoothInputPlugin = lib.mkOption {
      type = lib.types.bool;
      default = true;
      description = ''
        Run bluetoothd with `--noplugin=input`. vds needs raw ownership of the
        controller's Bluetooth HID channels; with the input plugin active,
        BlueZ claims the DualSense first and the app never sees it.

        Trade-off (upstream vds limitation): while the plugin is disabled,
        other Bluetooth input devices (keyboards, mice) will not work. Set to
        false if you need those and connect the controller over USB instead.
      '';
    };
  };

  config = lib.mkIf cfg.enable {
    boot.extraModulePackages = [
      (pkgs.callPackage ./vds-module.nix {
        kernel = config.boot.kernelPackages.kernel;
        inherit version;
      })
    ];

    boot.kernelModules = [
      "vds_hcd"
    ];

    users.groups.vds = { };
    users.users = lib.genAttrs vdsUsers (_: {
      extraGroups = [ "vds" ];
    });

    # Bluetooth is the transport this whole stack exists for.
    hardware.bluetooth.enable = lib.mkDefault true;

    systemd.services.bluetooth.serviceConfig.ExecStart =
      lib.mkIf cfg.disableBluetoothInputPlugin (lib.mkForce [
        ""
        "${config.hardware.bluetooth.package}/libexec/bluetooth/bluetoothd -f /etc/bluetooth/main.conf --noplugin=input"
      ]);

    boot.extraModprobeConfig = ''
      options vds_hcd max_port=${toString cfg.maxPorts}
    '';

    environment.systemPackages = [
      cfg.package
      effectiveVdsPackage
    ];

    services.udev.packages = [
      effectiveVdsPackage
    ];

    environment.etc."wireplumber/wireplumber.conf.d/99-vds-dualsense.conf".source = "${effectiveVdsPackage}/share/wireplumber/wireplumber.conf.d/99-vds-dualsense.conf";

    systemd.services.vdsd = {
      description = "vDS userspace daemon (OpenDS5)";

      after = [
        "bluetooth.service"
        "systemd-modules-load.service"
      ];

      wants = [
        "bluetooth.service"
      ];

      wantedBy = [
        "multi-user.target"
      ];

      serviceConfig = {
        Type = "simple";

        ExecStart = "${effectiveVdsPackage}/bin/vdsd";

        Restart = "on-failure";
        RestartSec = "1s";
      };
    };
  };
}
