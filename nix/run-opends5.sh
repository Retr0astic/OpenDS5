#!/bin/sh
set -eu

umask 077
PATH=@SAFE_PATH@
export PATH

# Reuse an installed service without authorization only when its declared
# executable is this flake's exact daemon and a command from the current
# protocol succeeds. Recheck activity after the probe to close the obvious
# stop/restart race; all uncertain cases use the privileged replacement path.
if @SYSTEMCTL@ is-active --quiet vdsd.service; then
  service_exec=$(@SYSTEMCTL@ show --property ExecStart --value vdsd.service 2>/dev/null || true)
  case "$service_exec" in
    *"{ path=@BUNDLE_VDSD@ ;"*|*"{ path=@STANDALONE_VDSD@ ;"*)
      if @VDSCTL@ audio-stats >/dev/null 2>&1 \
        && @SYSTEMCTL@ is-active --quiet vdsd.service; then
        exec @OPENDS5@ "$@"
      fi
      ;;
  esac
fi

if [ -x @NIXOS_PKEXEC@ ]; then
  pkexec_cmd=@NIXOS_PKEXEC@
elif [ -x @USR_PKEXEC@ ]; then
  pkexec_cmd=@USR_PKEXEC@
else
  echo "OpenDS5: no usable host pkexec wrapper was found (checked /run/wrappers/bin/pkexec and /usr/bin/pkexec)." >&2
  exit 69
fi

control_dir=$(@MKTEMP@ -d /tmp/opends5-run.XXXXXX)
control_fifo="$control_dir/control"
status_file="$control_dir/status"
helper_status_file="$control_dir/helper-status"
@MKFIFO@ "$control_fifo"
: >"$status_file"

helper_pid=
app_pid=
control_open=0
cleanup() {
  saved_status=$1
  trap - EXIT HUP INT TERM
  if [ -n "$app_pid" ]; then
    kill "$app_pid" 2>/dev/null || true
    wait "$app_pid" 2>/dev/null || true
  fi
  if [ "$control_open" -eq 1 ]; then
    exec 8>&-
    control_open=0
  fi
  if [ -n "$helper_pid" ]; then
    if wait "$helper_pid"; then
      :
    else
      helper_status=$?
      if [ "$saved_status" -eq 0 ]; then
        saved_status=$helper_status
      fi
    fi
  fi
  @RM@ -r "$control_dir"
  exit "$saved_status"
}
trap 'cleanup $?' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

# The asynchronous command opens only the FIFO's read end. The launcher then
# opens and owns the sole write end; closing fd 8 therefore provably sends EOF.
# Nix launchers can inherit a store-path SHELL that the host's setuid wrapper
# does not list in /etc/shells. The helper is an exact fixed path and does not
# consult this variable, so pass a conventional valid shell only to pkexec.
# Keep the background PID owned by this user even after pkexec changes the
# actual helper's credentials. The wrapper records pkexec's status atomically,
# so readiness never depends on signalling or inspecting a root process.
(
  if SHELL=/bin/sh "$pkexec_cmd" @ROOT_HELPER@ <"$control_fifo" >"$status_file"; then
    helper_status=0
  else
    helper_status=$?
  fi
  printf '%s\n' "$helper_status" >"$helper_status_file.tmp"
  @MV@ "$helper_status_file.tmp" "$helper_status_file"
  exit "$helper_status"
) &
helper_pid=$!
exec 8>"$control_fifo"
control_open=1

deadline=100
while [ "$deadline" -gt 0 ]; do
  if [ -f "$helper_status_file" ]; then
    read -r helper_status <"$helper_status_file"
    wait "$helper_pid" 2>/dev/null || true
    helper_pid=
    if [ "$helper_status" -eq 0 ]; then
      helper_status=70
    fi
    echo "OpenDS5: authorization was cancelled or the bundled daemon helper exited before readiness." >&2
    exit "$helper_status"
  fi
  if @GREP@ -Fxq OPENDS5_READY "$status_file"; then
    break
  fi
  @SLEEP@ 0.1
  deadline=$((deadline - 1))
done
if [ "$deadline" -eq 0 ]; then
  echo "OpenDS5: bundled vdsd did not become ready within 10 seconds." >&2
  exit 70
fi

# Deliberately permit normal application arguments; they are passed directly,
# never evaluated by a shell.
@OPENDS5@ "$@" 8>&- &
app_pid=$!
if wait "$app_pid"; then app_status=0; else app_status=$?; fi
app_pid=
exit "$app_status"
