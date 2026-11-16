# OS Big Assignment 2 — Testing & Validation

## Test Environment
- OS: Ubuntu 24.04
- Project: hk261_ossim_demacOprx
- Tested commit: 29c6b5e
- Build: make os
- Test runner: tests/run_tests.sh

## Queue Unit Tests
- Q-001 to Q-009: 9/9 PASS (previous manual run)
- Automated queue result: see tests/evidence/queue_results.txt

## Scheduler Tests

| ID | Description | Result |
|---|---|---|
| SCHED-001 | Single process | PASS |
| SCHED-002 | Round-Robin | PASS |
| SCHED-003 | MLQ priority | PASS |
| SCHED-004 | Multi-CPU | PASS |
| SCHED-005 | Empty Queue / CPU Idle | XFAIL |
| SCHED-006 | MLQ quota | PASS |
| SCHED-007 | Stability, 10 runs | PASS |

Scheduler: 6 PASS, 1 expected failure.
Unexpected failures: 0.

## Integration Test
RT-001: FAIL — SIGSEGV (exit 139).

## Known Bugs

### BUG-002 — Memory Initialization
- Location: src/libmem.c:644
- Observed: cur_vma == NULL
- Observed: caller->mm->mmap == NULL
- Result: Segmentation Fault
- Suspected upstream cause: incomplete init_mm()
- Status: Open

### BUG-003 — CPU Idle Infinite Loop
- Location: src/os.c, cpu_routine()
- Reproduction: os_test_idle
- Observed: CPU 1 finishes, CPU 0 does not stop
- Result: Timeout (exit 124)
- Suspected cause: continue skips completion check
- Status: Open

## Conclusion
The tested Scheduler implementation passes its
basic functional tests, MLQ quota test, and
10-run Multi-CPU stability test.

Known bugs remain unresolved. Full system
correctness has not yet been established.

No changes have been pushed to GitHub.
