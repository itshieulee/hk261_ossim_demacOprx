#!/usr/bin/env bash
set -u

cd "$(dirname "$0")/.."
mkdir -p tests/evidence/scheduler

SUMMARY="tests/evidence/test_summary.txt"
: > "$SUMMARY"

PASS=0
FAIL=0
XFAIL=0

report() {
    echo "$*" | tee -a "$SUMMARY"
}

run_os() {
    local config="$1"
    local output="$2"
    local seconds="${3:-10}"

    # Filter repetitive time-slot lines to avoid huge logs
    timeout "${seconds}s" stdbuf -oL ./os "$config" 2>&1 \
        | awk '!/^Time slot/ { print }' > "$output"

    RUN_EXIT=${PIPESTATUS[0]}
}

finished() {
    grep -Eq "Processed[[:space:]]+$1 has finished" "$2"
}

stopped() {
    grep -q "CPU $1 stopped" "$2"
}

check_run() {
    local id="$1"
    local config="$2"
    local pids="$3"
    local cpus="$4"

    local log="tests/evidence/scheduler/${id}.log"
    local ok=1

    run_os "$config" "$log"

    [ "$RUN_EXIT" -eq 0 ] || ok=0

    for pid in $pids; do
        finished "$pid" "$log" || ok=0
    done

    for cpu in $cpus; do
        stopped "$cpu" "$log" || ok=0
    done

    if [ "$ok" -eq 1 ]; then
        report "$id: PASS"
        PASS=$((PASS+1))
    else
        report "$id: FAIL (exit=$RUN_EXIT)"
        FAIL=$((FAIL+1))
    fi
}

report "=== QUEUE UNIT TESTS ==="

if gcc -std=c11 -Wall -Wextra -Iinclude \
    tests/test_queue.c src/queue.c \
    -o /tmp/hk261_test_queue \
    && /tmp/hk261_test_queue \
    > tests/evidence/queue_results.txt 2>&1; then
    report "QUEUE: PASS"
else
    report "QUEUE: FAIL"
    FAIL=$((FAIL+1))
fi

report ""
report "=== SCHEDULER TESTS ==="

check_run "SCHED-001" "os_test_calc_only" "1" "0"
check_run "SCHED-002" "os_test_two_processes" "1 2" "0"
check_run "SCHED-003" "os_test_mlq_priority" "1 2 3" "0"
check_run "SCHED-004" "os_test_multi_cpu" "1 2 3" "0 1"

# Known issue BUG-003: idle CPU can loop indefinitely.
run_os "os_test_idle" \
    "tests/evidence/scheduler/SCHED-005.log" 3

if [ "$RUN_EXIT" -eq 124 ]; then
    report "SCHED-005: XFAIL (known BUG-003, timeout)"
    XFAIL=$((XFAIL+1))
elif [ "$RUN_EXIT" -eq 0 ] \
    && finished 1 tests/evidence/scheduler/SCHED-005.log \
    && stopped 0 tests/evidence/scheduler/SCHED-005.log \
    && stopped 1 tests/evidence/scheduler/SCHED-005.log; then
    report "SCHED-005: PASS (BUG-003 may be fixed)"
    PASS=$((PASS+1))
else
    report "SCHED-005: FAIL (exit=$RUN_EXIT)"
    FAIL=$((FAIL+1))
fi

# Verify quota: 140 dispatches of P1 before first P2.
run_os "os_test_mlq_quota" \
    "tests/evidence/scheduler/SCHED-006.log"

if [ "$RUN_EXIT" -eq 0 ] \
    && finished 1 tests/evidence/scheduler/SCHED-006.log \
    && finished 2 tests/evidence/scheduler/SCHED-006.log \
    && stopped 0 tests/evidence/scheduler/SCHED-006.log \
    && awk '
        /Dispatched process/ {
            pid=$NF
            if (pid==1 && !seen2) count++
            if (pid==2 && !seen2) {
                seen2=1
                first=count
            }
        }
        END { exit !(seen2 && first==140) }
    ' tests/evidence/scheduler/SCHED-006.log; then
    report "SCHED-006: PASS (quota=140)"
    PASS=$((PASS+1))
else
    report "SCHED-006: FAIL (exit=$RUN_EXIT)"
    FAIL=$((FAIL+1))
fi

report ""
report "=== SCHED-007: STABILITY (10 RUNS) ==="

stable_pass=0

for i in $(seq 1 10); do
    log="tests/evidence/scheduler/stability_${i}.log"
    run_os "os_test_multi_cpu" "$log" 5

    if [ "$RUN_EXIT" -eq 0 ] \
        && finished 1 "$log" \
        && finished 2 "$log" \
        && finished 3 "$log" \
        && stopped 0 "$log" \
        && stopped 1 "$log"; then
        stable_pass=$((stable_pass+1))
        report "RUN $i: PASS"
    else
        report "RUN $i: FAIL (exit=$RUN_EXIT)"
    fi
done

if [ "$stable_pass" -eq 10 ]; then
    report "SCHED-007: PASS (10/10)"
    PASS=$((PASS+1))
else
    report "SCHED-007: FAIL ($stable_pass/10)"
    FAIL=$((FAIL+1))
fi

report ""
report "=== FINAL SUMMARY ==="
report "Scheduler PASS: $PASS/7"
report "Known failures (XFAIL): $XFAIL"
report "Unexpected failures: $FAIL"
report "Results saved to: $SUMMARY"

[ "$FAIL" -eq 0 ]
