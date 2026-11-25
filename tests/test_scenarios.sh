#!/usr/bin/env bash
set -euo pipefail
set +m

root=$(cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d)
watchdog_pid=
log=

cleanup() {
    if [[ -n $watchdog_pid ]]; then
        # setsid gives each test watchdog and its workers a private group.
        kill -KILL -- "-$watchdog_pid" 2>/dev/null || true
        wait "$watchdog_pid" 2>/dev/null || true
    fi
    rm -rf -- "$tmp"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

fail() {
    printf 'FAIL: %s\n' "$1" >&2
    if [[ -f $log ]]; then
        cat -- "$log" >&2
    fi
    if [[ -f watchdog.stderr ]]; then
        cat watchdog.stderr >&2
    fi
    exit 1
}

wait_for_log() {
    local pattern=$1
    for ((attempt=0; attempt<150; attempt++)); do
        if [[ -f $log ]] && grep -Eq -- "$pattern" "$log"; then
            return
        fi
        kill -0 "$watchdog_pid" 2>/dev/null || break
        sleep 0.1
    done
    # The final line can arrive between grep and the exit of the watchdog.
    grep -Eq -- "$pattern" "$log" 2>/dev/null || fail "missing log line: $pattern"
}

start_watchdog() {
    mkdir -p -- "$tmp/$1/workers"
    cd -- "$tmp/$1"
    ln -s -- "$root/workers/worker" workers/worker
    log="$PWD/watchdog.log"
    setsid "$root/watchdog" >watchdog.stdout 2>watchdog.stderr &
    watchdog_pid=$!
    wait_for_log ' START name=[^ ]+ pid=[1-9][0-9]* mode=loop$'
}

verify_worker() {
    local pid=$1 parent group
    [[ $pid =~ ^[1-9][0-9]*$ ]] || fail "invalid worker PID: $pid"
    read -r parent group < <(ps -o ppid=,pgid= -p "$pid") ||
        fail "worker $pid is missing"
    [[ $parent == "$watchdog_pid" && $group == "$watchdog_pid" ]] ||
        fail "PID $pid is outside the test watchdog"
    # START and RESTART may be logged just before the child's exec().
    for ((attempt=0; attempt<20; attempt++)); do
        if [[ $(readlink -- "/proc/$pid/exe") == "$root/workers/worker" ]]; then
            return
        fi
        sleep 0.05
    done
    fail "PID $pid is running a different executable"
}

shutdown() {
    local sig=$1
    shift
    kill -s "$sig" "$watchdog_pid" || fail "cannot send SIG$sig"
    wait_for_log ' SHUTDOWN_DONE$'
    for ((attempt=0; attempt<50; attempt++)); do
        kill -0 "$watchdog_pid" 2>/dev/null || break
        sleep 0.1
    done
    ! kill -0 "$watchdog_pid" 2>/dev/null || fail "watchdog did not exit"
    wait "$watchdog_pid" || fail "watchdog exited with an error"
    for pid in "$@"; do
        [[ ! -e /proc/$pid ]] || fail "child $pid is still present"
    done
    ! kill -0 -- "-$watchdog_pid" 2>/dev/null || fail "test process group still exists"
    watchdog_pid=
    grep -Eq ' SHUTDOWN( |$)' "$log" || fail "missing shutdown start"
}

start_watchdog lifecycle
read -r loop_name worker_pid < <(
    sed -nE 's/.* START name=([^ ]+) pid=([0-9]+) mode=loop$/\1 \2/p' "$log" | head -n 1
)
wait_for_log ' START name=[^ ]+ pid=[1-9][0-9]* mode=exit$'
verify_worker "$worker_pid"
[[ $(grep -c ' START name=' "$log") -ge 2 ]] || fail "fewer than two workers started"
exit_name=$(sed -nE 's/.* START name=([^ ]+) pid=[0-9]+ mode=exit$/\1/p' "$log" | head -n 1)

kill -USR1 "$watchdog_pid"
wait_for_log ' STATUS_END$'
grep -Fq " STATUS name=$loop_name pid=$worker_pid state=RUNNING restarts=0" "$log" ||
    fail "running worker missing from SIGUSR1 report"
grep -Fq " STATUS name=$exit_name pid=" "$log" ||
    fail "normal-exit worker missing from SIGUSR1 report"

wait_for_log " EXIT name=$exit_name pid=[1-9][0-9]* code=0$"
for restart in 1 2 3 4; do
    verify_worker "$worker_pid"
    kill -KILL "$worker_pid"
    wait_for_log " CRASH name=$loop_name pid=$worker_pid signal=9$"
    if ((restart <= 3)); then
        wait_for_log " RESTART name=$loop_name pid=[1-9][0-9]* attempt=$restart$"
        worker_pid=$(sed -nE "s/.* RESTART name=$loop_name pid=([0-9]+) attempt=$restart$/\1/p" "$log")
    fi
done
wait_for_log " RESTART_LIMIT name=$loop_name restarts=3$"
shutdown TERM "$worker_pid"
! grep -Fq " RESTART name=$exit_name " "$log" || fail "normal exit was restarted"
[[ $(grep -Fc " RESTART name=$loop_name " "$log") -eq 3 ]] ||
    fail "expected exactly three restarts"
if grep -Evq '^\[[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\] [A-Z_]+( |$)' "$log"; then
    fail "malformed timestamp or event name"
fi
echo "OK: startup, SIGUSR1, normal exit, unexpected exit and restart limit"

for sig in TERM INT; do
    start_watchdog "$sig"
    worker_pid=$(sed -nE 's/.* START name=[^ ]+ pid=([0-9]+) mode=loop$/\1/p' "$log" | head -n 1)
    verify_worker "$worker_pid"
    shutdown "$sig" "$worker_pid"
    echo "OK: SIG$sig shuts down and reaps the running worker"
done
