#!/bin/sh
# Run the MMUKO runtime and wait for the process-isolation layer to finish.
#
# usage: run_runtime.sh LOG COMMAND [ARGS...]
#
# The bootstrap parent retires as soon as the execution domain has registered
# both contexts, so its exit is not the end of the run. Wait for the
# supervisor's final line instead of sleeping a fixed time.
set -u

log=$1
shift
rm -f "$log"

if ! "$@" >"$log" 2>&1; then
    echo "run_runtime: bootstrap exited non-zero" >&2
    cat "$log" >&2
    exit 1
fi

tries=0
until grep -q '^\[program\] supervisor exiting' "$log"; do
    tries=$((tries + 1))
    if [ "$tries" -gt 300 ]; then
        echo "run_runtime: timed out waiting for the supervisor to exit" >&2
        cat "$log" >&2
        exit 1
    fi
    sleep 0.1
done
