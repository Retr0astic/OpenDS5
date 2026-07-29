#!/bin/sh
set -eu

src_dir=${1:?source directory required}
sleep_cmd=$(command -v sleep)
host_bin=${sleep_cmd%/*}
work=$(mktemp -d /tmp/opends5-launcher-test.XXXXXX)
trap 'rm -r "$work"' EXIT HUP INT TERM
mkdir -p "$work/bin" "$work/module"
: >"$work/device"
: >"$work/log"

make_mock() {
  name=$1
  shift
  {
    echo '#!/bin/sh'
    printf '%s\n' "$@"
  } >"$work/bin/$name"
  chmod +x "$work/bin/$name"
}

make_mock systemctl '
echo "systemctl $*" >>"$TEST_LOG"
case "$1" in
  is-active)
    if [ "${SERVICE_RACE:-0}" = 1 ]; then
      if [ ! -e "$TEST_WORK/active-checked" ]; then
        printf '1\n' >"$TEST_WORK/active-checked"
      else
        exit 1
      fi
    fi
    [ "${SERVICE_ACTIVE:-0}" = 1 ] && [ ! -e "$TEST_WORK/stopped" ] ;;
  show)
    case "${SERVICE_MATCH:-other}" in
      bundle) service_path=$TEST_WORK/bin/vdsd-bundle ;;
      standalone) service_path=$TEST_WORK/bin/vdsd-standalone ;;
      *) service_path=/old/vdsd ;;
    esac
    printf "{ path=%s ; argv[]=%s ; ignore_errors=no ; }\n" "$service_path" "$service_path" ;;
  stop) [ "${STOP_FAIL:-0}" != 1 ] || exit 1; : >"$TEST_WORK/stopped" ;;
  start) [ "${RESTORE_FAIL:-0}" != 1 ] || exit 1; : >"$TEST_WORK/restored" ;;
esac'
make_mock modprobe 'exit "${MODPROBE_STATUS:-0}"'
make_mock vdsctl '
[ "$1" = audio-stats ] && [ "${SERVICE_PROTOCOL:-1}" = 0 ] && exit 1
[ "${SERVICE_RACE:-0}" = 1 ] && [ -e "$TEST_WORK/active-checked" ] && [ ! -e "$TEST_WORK/daemon-running" ] && exit 1
[ "${SERVICE_ACTIVE:-0}" = 1 ] && [ ! -e "$TEST_WORK/stopped" ] && exit 0
[ "${OLD_RESPONDER:-0}" = 1 ] && exit 0
[ -e "$TEST_WORK/daemon-running" ] && [ "${READY_FAIL:-0}" != 1 ]'
make_mock vdsd '
[ "${DAEMON_FAIL:-0}" != 1 ] || exit 42
: >"$TEST_WORK/daemon-running"
trap '\''rm -f "$TEST_WORK/daemon-running"; echo daemon-cleanup >>"$TEST_LOG"; exit 0'\'' TERM INT
echo daemon-start >>"$TEST_LOG"
while :; do sleep 1; done'
make_mock flock '
if [ "${LOCK_FAIL:-0}" = 1 ]; then exit 1; fi
exit 0'
make_mock pkexec-nixos '
echo nixos-pkexec >>"$TEST_LOG"
[ "${SHELL:-}" = /bin/sh ] || { echo invalid-auth-shell >>"$TEST_LOG"; exit 125; }
if [ "${AUTH_FAIL:-0}" = 1 ]; then exit 126; fi
"$@" &
privileged_pid=$!
echo "privileged-pid=$privileged_pid wrapper-pid=$PPID" >>"$TEST_LOG"
if wait "$privileged_pid"; then exit 0; else exit $?; fi'
make_mock pkexec-usr '
echo usr-pkexec >>"$TEST_LOG"
[ "${SHELL:-}" = /bin/sh ] || { echo invalid-auth-shell >>"$TEST_LOG"; exit 125; }
if [ "${AUTH_FAIL:-0}" = 1 ]; then exit 126; fi
"$@" &
privileged_pid=$!
echo "privileged-pid=$privileged_pid wrapper-pid=$PPID" >>"$TEST_LOG"
if wait "$privileged_pid"; then exit 0; else exit $?; fi'
make_mock app '
echo app >>"$TEST_LOG"
echo "app-shell=${SHELL-}" >>"$TEST_LOG"
if [ "${APP_DESCENDANT:-0}" = 1 ]; then sleep 20 & echo $! >"$TEST_WORK/descendant-pid"; fi
[ "${APP_WAIT:-0}" != 1 ] || while :; do sleep 1; done
exit "${APP_STATUS:-0}"'

substitute() {
  source=$1
  target=$2
  sed \
    -e "s|@SAFE_PATH@|$work/bin:$host_bin|g" \
    -e "s|@LOCK_FILE@|$work/lock|g" \
    -e "s|@MODULE_PATH@|$work/module|g" \
    -e "s|@DEVICE_PATH@|$work/device|g" \
    -e "s|@FLOCK@|$work/bin/flock|g" \
    -e "s|@SYSTEMCTL@|$work/bin/systemctl|g" \
    -e "s|@BUNDLE_VDSD@|$work/bin/vdsd-bundle|g" \
    -e "s|@STANDALONE_VDSD@|$work/bin/vdsd-standalone|g" \
    -e "s|@MODPROBE@|$work/bin/modprobe|g" \
    -e "s|@VDSD@|$work/bin/vdsd|g" \
    -e "s|@VDSCTL@|$work/bin/vdsctl|g" \
    -e "s|@SLEEP@|$sleep_cmd|g" \
    -e "s|@NIXOS_PKEXEC@|$work/bin/pkexec-nixos|g" \
    -e "s|@USR_PKEXEC@|$work/bin/pkexec-usr|g" \
    -e "s|@MKTEMP@|@MKTEMP@|g" \
    -e "s|@MKFIFO@|@MKFIFO@|g" \
    -e "s|@MV@|@MV@|g" \
    -e "s|@RM@|@RM@|g" \
    -e "s|@GREP@|@GREP@|g" \
    -e "s|@ROOT_HELPER@|$work/root-helper|g" \
    -e "s|@OPENDS5@|$work/bin/app|g" \
    "$source" >"$target"
  chmod +x "$target"
}

substitute "$src_dir/run-vdsd-root.sh" "$work/root-helper"
sed -i \
  -e "s|@MKTEMP@|$(command -v mktemp)|g" \
  -e "s|@MKFIFO@|$(command -v mkfifo)|g" \
  -e "s|@MV@|$(command -v mv)|g" \
  -e "s|@RM@|$(command -v rm)|g" \
  -e "s|@GREP@|$(command -v grep)|g" "$work/root-helper"
substitute "$src_dir/run-opends5.sh" "$work/launcher"
sed -i \
  -e "s|@MKTEMP@|$(command -v mktemp)|g" \
  -e "s|@MKFIFO@|$(command -v mkfifo)|g" \
  -e "s|@MV@|$(command -v mv)|g" \
  -e "s|@RM@|$(command -v rm)|g" \
  -e "s|@GREP@|$(command -v grep)|g" "$work/launcher"

export TEST_WORK=$work TEST_LOG=$work/log

run_case() {
  name=$1
  shift
  : >"$work/log"
  rm -f "$work/stopped" "$work/restored"
  rm -f "$work/daemon-running" "$work/descendant-pid" "$work/active-checked"
  "$@"
  echo "ok - $name"
}

run_case nix-shell-sanitized env SHELL=/nix/store/test-shell/bin/sh SERVICE_ACTIVE=1 "$work/launcher"
grep -q nixos-pkexec "$work/log"
! grep -q usr-pkexec "$work/log"
! grep -q invalid-auth-shell "$work/log"
grep -q 'app-shell=/nix/store/test-shell/bin/sh' "$work/log"
grep -q 'systemctl stop vdsd.service' "$work/log"
test -e "$work/restored"
grep -q daemon-cleanup "$work/log"
grep -Eq 'privileged-pid=[0-9]+ wrapper-pid=[0-9]+' "$work/log"
grep -q '^app$' "$work/log"

run_case bundle-service-reused env SERVICE_ACTIVE=1 SERVICE_MATCH=bundle "$work/launcher"
grep -q '^app$' "$work/log"
! grep -q pkexec "$work/log"
! grep -q 'systemctl stop' "$work/log"
! grep -q daemon-start "$work/log"

run_case standalone-service-reused env SERVICE_ACTIVE=1 SERVICE_MATCH=standalone "$work/launcher"
grep -q '^app$' "$work/log"
! grep -q pkexec "$work/log"
! grep -q 'systemctl stop' "$work/log"
! grep -q daemon-start "$work/log"

run_case matching-service-protocol-mismatch-replaced env SERVICE_ACTIVE=1 SERVICE_MATCH=bundle SERVICE_PROTOCOL=0 "$work/launcher"
grep -q nixos-pkexec "$work/log"
grep -q 'systemctl stop vdsd.service' "$work/log"
grep -q daemon-start "$work/log"

run_case matching-service-race-replaced env SERVICE_ACTIVE=1 SERVICE_MATCH=bundle SERVICE_RACE=1 "$work/launcher"
grep -q nixos-pkexec "$work/log"
grep -q daemon-start "$work/log"

run_case mismatched-service-replaced env SERVICE_ACTIVE=1 SERVICE_MATCH=other "$work/launcher"
grep -q nixos-pkexec "$work/log"
grep -q 'systemctl stop vdsd.service' "$work/log"
grep -q daemon-start "$work/log"

run_case inactive-stays-inactive env SERVICE_ACTIVE=0 "$work/launcher"
test ! -e "$work/restored"
! grep -q 'systemctl stop vdsd.service' "$work/log"

rm -f "$work/stopped" "$work/restored" "$work/daemon-running"
if env SERVICE_ACTIVE=1 STOP_FAIL=1 "$work/launcher" >/dev/null 2>&1; then exit 1; fi
echo 'ok - stop failure'
if env LOCK_FAIL=1 "$work/launcher" >/dev/null 2>&1; then exit 1; fi
echo 'ok - concurrent lock'
if timeout 3s env AUTH_FAIL=1 "$work/launcher" >"$work/auth.out" 2>&1; then exit 1; fi
grep -q 'helper exited before readiness' "$work/auth.out"
echo 'ok - auth cancellation'
chmod -x "$work/bin/pkexec-nixos"
run_case conventional-pkexec-fallback "$work/launcher"
grep -q usr-pkexec "$work/log"
chmod +x "$work/bin/pkexec-nixos"
chmod -x "$work/bin/pkexec-nixos" "$work/bin/pkexec-usr"
if "$work/launcher" >/dev/null 2>&1; then exit 1; else test "$?" -eq 69; fi
chmod +x "$work/bin/pkexec-nixos" "$work/bin/pkexec-usr"
echo 'ok - unavailable auth tool'
if env READY_FAIL=1 "$work/launcher" >/dev/null 2>&1; then exit 1; fi
echo 'ok - readiness failure'
if timeout 3s env DAEMON_FAIL=1 "$work/launcher" >"$work/daemon-fail.out" 2>&1; then exit 1; fi
grep -q 'helper exited before readiness' "$work/daemon-fail.out"
echo 'ok - daemon early exit'
if env OLD_RESPONDER=1 "$work/launcher" >/dev/null 2>&1; then exit 1; fi
! grep -q 'systemctl stop vdsd.service' "$work/log"
! grep -q daemon-start "$work/log"
echo 'ok - old responder blocks bundled daemon start'

if env APP_STATUS=23 "$work/launcher" >/dev/null 2>&1; then exit 1; else test "$?" -eq 23; fi
echo 'ok - app failure status preserved'
rm -f "$work/stopped" "$work/restored" "$work/daemon-running"
if env SERVICE_ACTIVE=1 RESTORE_FAIL=1 "$work/launcher" >/dev/null 2>&1; then exit 1; else test "$?" -eq 74; fi
echo 'ok - restore failure surfaced'
rm -f "$work/stopped" "$work/restored" "$work/daemon-running"
if env SERVICE_ACTIVE=1 RESTORE_FAIL=1 APP_STATUS=23 "$work/launcher" >/dev/null 2>&1; then exit 1; else test "$?" -eq 23; fi
echo 'ok - app failure takes precedence over restore failure'

: >"$work/log"
timeout 3s env APP_DESCENDANT=1 "$work/launcher"
grep -q daemon-cleanup "$work/log"
descendant_pid=$(cat "$work/descendant-pid")
kill "$descendant_pid" 2>/dev/null || true
echo 'ok - app descendant cannot retain control writer'

: >"$work/log"
rm -f "$work/stopped" "$work/restored" "$work/daemon-running"
env APP_WAIT=1 "$work/launcher" >/dev/null 2>&1 &
launcher_pid=$!
deadline=30
while [ ! -e "$work/daemon-running" ] && [ "$deadline" -gt 0 ]; do sleep 0.1; deadline=$((deadline - 1)); done
kill -TERM "$launcher_pid"
if wait "$launcher_pid"; then exit 1; else test "$?" -eq 143; fi
grep -q daemon-cleanup "$work/log"
echo 'ok - TERM cleans app and helper'

: >"$work/log"
"$work/launcher"
grep -q daemon-cleanup "$work/log"
echo 'ok - launcher fd EOF cleans helper child'
