#!/bin/sh
set -eu

if [ "$#" -ne 0 ]; then
  echo "OpenDS5: the privileged daemon helper accepts no arguments." >&2
  exit 64
fi

umask 077
PATH=@SAFE_PATH@
export PATH

exec 9>@LOCK_FILE@
if ! @FLOCK@ -n 9; then
  echo "OpenDS5: another temporary vdsd launcher is already running." >&2
  exit 73
fi

restore_service=0
daemon_pid=
cleanup() {
  saved_status=$1
  trap - EXIT HUP INT TERM
  if [ -n "$daemon_pid" ]; then
    kill "$daemon_pid" 2>/dev/null || true
    wait "$daemon_pid" 2>/dev/null || true
  fi
  if [ "$restore_service" -eq 1 ]; then
    if ! @SYSTEMCTL@ start vdsd.service; then
      echo "OpenDS5: failed to restore vdsd.service; run 'sudo systemctl start vdsd.service'." >&2
      saved_status=74
    fi
  fi
  exit "$saved_status"
}
trap 'cleanup $?' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

if @SYSTEMCTL@ is-active --quiet vdsd.service; then
  restore_service=1
  if ! @SYSTEMCTL@ stop vdsd.service; then
    echo "OpenDS5: failed to stop vdsd.service." >&2
    exit 69
  fi
  if @SYSTEMCTL@ is-active --quiet vdsd.service; then
    echo "OpenDS5: vdsd.service remained active after stop." >&2
    exit 69
  fi
fi
if @VDSCTL@ list >/dev/null 2>&1; then
  echo "OpenDS5: another vdsd daemon still responds after vdsd.service stopped; stop the manual or stale daemon first." >&2
  exit 69
fi

if [ ! -d @MODULE_PATH@ ]; then
  @MODPROBE@ vds_hcd || true
fi
if [ ! -d @MODULE_PATH@ ] || [ ! -e @DEVICE_PATH@ ]; then
  echo "OpenDS5: vds_hcd or /dev/vds0 is unavailable; install/enable the OpenDS5 kernel module." >&2
  exit 69
fi

@VDSD@ &
daemon_pid=$!
deadline=100
while [ "$deadline" -gt 0 ]; do
  if ! kill -0 "$daemon_pid" 2>/dev/null; then
    echo "OpenDS5: bundled vdsd exited before readiness." >&2
    if wait "$daemon_pid"; then exit 70; else exit $?; fi
  fi
  if @VDSCTL@ list >/dev/null 2>&1; then
    if ! kill -0 "$daemon_pid" 2>/dev/null; then
      echo "OpenDS5: bundled vdsd exited during its readiness check." >&2
      if wait "$daemon_pid"; then exit 70; else exit $?; fi
    fi
    echo OPENDS5_READY
    break
  fi
  @SLEEP@ 0.1
  deadline=$((deadline - 1))
done
if [ "$deadline" -eq 0 ]; then
  echo "OpenDS5: bundled vdsd failed its readiness check." >&2
  exit 70
fi

# stdin has only the launcher's write end as a peer. EOF is the transaction
# boundary; no commands or user-controlled paths are accepted over this pipe.
while IFS= read -r _ignored; do :; done
