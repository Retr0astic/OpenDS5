{
  description = "OpenDS5 — DualSense companion application and virtual DualSense stack";

  nixConfig = {
    extra-substituters = [
      "https://opends5.cachix.org"
    ];
    extra-trusted-public-keys = [
      "opends5.cachix.org-1:IzUxvZYBhuASyX7O7c1AkqNT3NiLtbF5X0eAwFCM95g="
    ];
  };

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = {
    self,
    nixpkgs,
  }: let
    version =
      (builtins.fromJSON (
        builtins.readFile ./ds5-bridge/companion/package.json
      )).version;

    systems = [
      "x86_64-linux"
      "aarch64-linux"
    ];

    forAllSystems = f:
      nixpkgs.lib.genAttrs systems (
        system:
          f nixpkgs.legacyPackages.${system}
      );
  in {
    packages = forAllSystems (
      pkgs: rec {
        vds = pkgs.callPackage ./nix/vds.nix {
          inherit version;
        };

        opends5 = pkgs.callPackage ./nix/opends5.nix {
          inherit version;
          vds = vds;
        };

        vds-module = pkgs.callPackage ./nix/vds-module.nix {
          kernel = pkgs.linuxPackages.kernel;
          inherit version;
        };

        default = opends5;
      }
    );

    apps = forAllSystems (
      pkgs: let
        bundle = self.packages.${pkgs.stdenv.hostPlatform.system}.opends5;
        rootHelper = pkgs.runCommand "opends5-vdsd-root" { } ''
          substitute ${./nix/run-vdsd-root.sh} $out \
            --replace-fail @SAFE_PATH@ ${pkgs.lib.makeBinPath [ pkgs.coreutils pkgs.util-linux pkgs.systemd pkgs.kmod ]} \
            --replace-fail @LOCK_FILE@ /run/lock/opends5-vdsd.lock \
            --replace-fail @MODULE_PATH@ /sys/module/vds_hcd \
            --replace-fail @DEVICE_PATH@ /dev/vds0 \
            --replace-fail @FLOCK@ ${pkgs.util-linux}/bin/flock \
            --replace-fail @SYSTEMCTL@ ${pkgs.systemd}/bin/systemctl \
            --replace-fail @MODPROBE@ ${pkgs.kmod}/bin/modprobe \
            --replace-fail @VDSD@ ${self.packages.${pkgs.stdenv.hostPlatform.system}.vds}/bin/vdsd \
            --replace-fail @VDSCTL@ ${self.packages.${pkgs.stdenv.hostPlatform.system}.vds}/bin/vdsctl \
            --replace-fail @SLEEP@ ${pkgs.coreutils}/bin/sleep
          chmod 0555 $out
        '';
        launcher = pkgs.runCommand "opends5-run" { } ''
          mkdir -p $out/bin
          substitute ${./nix/run-opends5.sh} $out/bin/opends5-run \
            --replace-fail @SAFE_PATH@ ${pkgs.lib.makeBinPath [ pkgs.coreutils pkgs.gnugrep ]} \
            --replace-fail @SYSTEMCTL@ ${pkgs.systemd}/bin/systemctl \
            --replace-fail @BUNDLE_VDSD@ ${self.packages.${pkgs.stdenv.hostPlatform.system}.vds}/bin/vdsd \
            --replace-fail @STANDALONE_VDSD@ ${self.packages.${pkgs.stdenv.hostPlatform.system}.vds}/bin/vdsd \
            --replace-fail @VDSCTL@ ${self.packages.${pkgs.stdenv.hostPlatform.system}.vds}/bin/vdsctl \
            --replace-fail @NIXOS_PKEXEC@ /run/wrappers/bin/pkexec \
            --replace-fail @USR_PKEXEC@ /usr/bin/pkexec \
            --replace-fail @MKTEMP@ ${pkgs.coreutils}/bin/mktemp \
            --replace-fail @MKFIFO@ ${pkgs.coreutils}/bin/mkfifo \
            --replace-fail @MV@ ${pkgs.coreutils}/bin/mv \
            --replace-fail @RM@ ${pkgs.coreutils}/bin/rm \
            --replace-fail @SLEEP@ ${pkgs.coreutils}/bin/sleep \
            --replace-fail @GREP@ ${pkgs.gnugrep}/bin/grep \
            --replace-fail @ROOT_HELPER@ ${rootHelper} \
            --replace-fail @OPENDS5@ ${bundle}/bin/opends5
          chmod 0555 $out/bin/opends5-run
        '';
      in {
        opends5-portable = {
          type = "app";
          program = "${launcher}/bin/opends5-run";
          meta.description = "Run OpenDS5 with its bundled temporary vdsd daemon";
        };
        opends5 = {
          type = "app";
          program = "${bundle}/bin/opends5";
          meta.description = "Run the OpenDS5 GUI against the configured system daemon";
        };
        default = self.apps.${pkgs.stdenv.hostPlatform.system}.opends5;
      }
    );

    checks = forAllSystems (
      pkgs: let
        bundle = self.packages.${pkgs.stdenv.hostPlatform.system}.opends5;
        vds = self.packages.${pkgs.stdenv.hostPlatform.system}.vds;
        customCompanion = pkgs.runCommand "custom-companion" { } "mkdir -p $out/bin";
        customVds = pkgs.runCommand "custom-vds" { } "mkdir -p $out/bin $out/share/wireplumber/wireplumber.conf.d";
        evalModule = extra: (nixpkgs.lib.nixosSystem {
          system = pkgs.stdenv.hostPlatform.system;
          modules = [ self.nixosModules.opends5 ({ ... }: {
            services.opends5 = { enable = true; } // extra;
            system.stateVersion = "25.11";
          }) ];
        }).config;
        defaultEval = evalModule { };
        packageOnlyEval = evalModule { package = customCompanion; };
        explicitEval = evalModule { package = customCompanion; vdsPackage = customVds; };
      in {
        bundle-layout = pkgs.runCommand "opends5-bundle-layout" { } ''
          test -x ${bundle}/bin/opends5
          test ! -e ${bundle}/bin/vdsd
          test ! -e ${bundle}/bin/vdsctl
          test ! -e ${bundle}/lib/udev
          test ! -e ${bundle}/lib/systemd
          test ! -e ${bundle}/share/wireplumber
          test -e ${bundle}/share/opends5/native/audio-helper-linux.mjs
          test -e ${bundle}/share/opends5/node_modules/node-hid
          test "$(sed -n 's/.*\"electron\": \"\^\([0-9]*\).*/\1/p' ${bundle}/share/opends5/package.json)" = 42
          touch $out
        '';
        launcher-lifecycle = pkgs.runCommand "opends5-launcher-lifecycle" { } ''
          ${pkgs.dash}/bin/dash ${./nix/test-run-launchers.sh} ${./nix}
          touch $out
        '';
        launcher-program = pkgs.runCommand "opends5-launcher-program" { } ''
          test -x ${self.apps.${pkgs.stdenv.hostPlatform.system}.opends5.program}
          touch $out
        '';
        electron-native-smoke = pkgs.runCommand "opends5-electron-native-smoke" { } ''
          electron="$(awk '/^exec / { print $2; exit }' ${bundle}/bin/opends5 | tr -d '"')"
          version="$($electron --version)"
          case "$version" in v42.*) ;; *) exit 1 ;; esac
          ${pkgs.nodejs}/bin/node -e "require('${bundle}/share/opends5/node_modules/node-hid')"
          touch $out
        '';
        module-package-policy = pkgs.runCommand "opends5-module-package-policy" { } ''
          test '${defaultEval.systemd.services.vdsd.serviceConfig.ExecStart}' = '${vds}/bin/vdsd'
          test '${builtins.head defaultEval.services.udev.packages}' = '${vds}'
          test '${builtins.head defaultEval.services.pipewire.wireplumber.configPackages}' = '${vds}'
          test '${packageOnlyEval.systemd.services.vdsd.serviceConfig.ExecStart}' = '${vds}/bin/vdsd'
          test '${explicitEval.systemd.services.vdsd.serviceConfig.ExecStart}' = '${customVds}/bin/vdsd'
          touch $out
        '';
      }
    );

    nixosModules = rec {
      opends5 = import ./nix/nixos-module.nix {
        inherit self version;
      };

      default = opends5;
    };

    devShells = forAllSystems (
      pkgs: {
        default = pkgs.mkShell {
          inputsFrom = [
            self.packages.${pkgs.stdenv.hostPlatform.system}.vds
            self.packages.${pkgs.stdenv.hostPlatform.system}.opends5
          ];

          packages = with pkgs; [
            fish
            nodejs
            git
            graphify
            ripgrep
            fd
            jq
            tree
            pkg-config
            cmake
            ninja
            gnumake
            gcc
            chromium
            evtest
            wayland-utils
            pipewire
            wireplumber
            gpu-screen-recorder
          ];

          shellHook = ''
            export OPENDS5_REPO_ROOT="''${OPENDS5_REPO_ROOT:-$PWD}"
            export npm_config_update_notifier=false

            # Keep the JavaScript toolchain available in development shells without
            # changing the checked-in lockfile or running during Nix/CI builds.
            companion_dir="$OPENDS5_REPO_ROOT/ds5-bridge/companion"
            if [[ -z "''${CI:-}" \
              && -f "$companion_dir/package-lock.json" \
              && ! -d "$companion_dir/node_modules/vite" ]]; then
              echo "OpenDS5: installing companion dependencies (node_modules is missing)..."
              if ! npm ci --prefix "$companion_dir"; then
                echo "OpenDS5: npm ci failed; retry from a network-enabled development shell." >&2
                exit 1
              fi
            fi

            if [[ $- == *i* && "$(${pkgs.coreutils}/bin/readlink /proc/$$/exe)" != */fish ]]; then
              exec ${pkgs.fish}/bin/fish
            fi
          '';
        };
      }
    );
  };
}
