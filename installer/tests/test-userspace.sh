#!/usr/bin/env bash
set -u
. "$(dirname "$0")/harness.sh"
here="$(cd "$(dirname "$0")" && pwd)"
tmp="$(mktemp -d)"

# fake userspace bundle
mkdir -p "$tmp/vds-bin"
for f in vdsd vdsctl; do : > "$tmp/vds-bin/$f"; done
printf 'ExecStart=@VDS_SYSTEMD_VDSD@\n' > "$tmp/vds-bin/vdsd.service.in"
: > "$tmp/vds-bin/99-vds-dualsense-udev.rules"
: > "$tmp/vds-bin/99-vds-dualsense-wireplumber.conf"
: > "$tmp/vds-bin/override-bluetoothd.sh"

plan() { # extra env...
  env "$@" OPENDS5_OS_RELEASE="$here/fixtures/fedora/os-release" \
      OPENDS5_UNAME_R=6.15.4-200.fc44.x86_64 OPENDS5_SB_STATE=disabled \
      OPENDS5_DRY_RUN=1 bash "$here/../opends5-install" --yes
}

# without bundle: no userspace steps (explicit override at an empty dir)
out="$(plan OPENDS5_SYSROOT=/nonexistent OPENDS5_USERSPACE_DIR=/nonexistent)"
case "$out" in *"/usr/local/bin/"*) f=1 ;; *) f=0 ;; esac
assert_eq 0 "$f" "no userspace steps without bundle"

# with bundle: all steps present
out="$(plan OPENDS5_SYSROOT=/nonexistent OPENDS5_USERSPACE_DIR="$tmp/vds-bin")"
installer_source="$(cat "$here/../opends5-install")"
assert_contains "$out" "install -m 0755 '$tmp/vds-bin/vdsd' '$tmp/vds-bin/vdsctl' /usr/local/bin/" "binaries installed"
assert_contains "$out" "s|@VDS_SYSTEMD_VDSD@|/usr/local/bin/vdsd|" "service template rendered"
assert_contains "$out" "/etc/systemd/system/vdsd.service" "unit installed"
assert_contains "$out" "/etc/udev/rules.d/" "udev rules installed"
assert_contains "$out" "udevadm control --reload-rules" "udev reloaded"
assert_contains "$out" "groupadd -f vds" "group created"
assert_contains "$out" "usermod -aG vds '$(id -un)'" "invoking user added to group"
assert_contains "$out" "wireplumber_install_root" "wireplumber conf installed"
assert_contains "$installer_source" "XDG_CONFIG_HOME" "invoking XDG config home is staged"
assert_contains "$installer_source" "XDG_CONFIG_DIRS" "XDG config precedence is staged"
assert_contains "$installer_source" "XDG_DATA_DIRS" "XDG data precedence is staged"
assert_contains "$installer_source" "preserving active package/system WirePlumber config" "package-managed config avoids user shadow override"
assert_contains "$installer_source" "99-vds-dualsense.conf" "system WirePlumber source is checked"
assert_contains "$installer_source" "refusing symlink in WirePlumber path" "symlink path is rejected"
assert_contains "$installer_source" "runuser -u" "user config operation drops root privileges"
assert_contains "$installer_source" "mkdir -m 0700" "missing config directories use safe mode"
assert_contains "$installer_source" "mktemp -p \"\$target_dir\" '.99-vds-dualsense-wireplumber.conf.tmp.XXXXXX'" "user config temp is exclusive and randomized"
assert_contains "$installer_source" "mv -f -- \"\$temp\" \"\$target\"" "user config install is same-directory atomic rename"
case "$installer_source" in *'chown "$user:"'*) f=1 ;; *) f=0 ;; esac
assert_eq 0 "$f" "user config has no root chown"
case "$installer_source" in *'install -m 0644 "$source" "$temp"'*) f=1 ;; *) f=0 ;; esac
assert_eq 0 "$f" "user config has no root install"
assert_contains "$out" "subsystem-match=sound" "sound devices are retriggered"
assert_contains "$out" "disable-input --restart" "bluez input plugin override planned"
assert_contains "$out" "noplugin=input" "override skipped when already applied"
assert_contains "$out" "systemctl daemon-reload" "daemon reloaded"
assert_contains "$out" "dkms install" "kernel steps still planned when module not loaded"

# kernel satisfied -> kernel steps skipped, userspace kept
mkdir -p "$tmp/sys/module/vds_hcd"
out="$(plan OPENDS5_SYSROOT="$tmp" OPENDS5_USERSPACE_DIR="$tmp/vds-bin")"
case "$out" in *"dkms install"*) f=1 ;; *) f=0 ;; esac
assert_eq 0 "$f" "kernel steps skipped when module already loaded"
assert_contains "$out" "/usr/local/bin/" "userspace steps still planned"
rm -rf "$tmp"
finish
