# OS Big Assignment 2 — Bug Reports

Test environment: Ubuntu 24.04
Tested commit: 29c6b5e
Status: Draft for team review

---

## BUG-002 — Memory Initialization / NULL Pointer

**Test:** RT-001
**Severity:** High
**Status:** Open

### Reproduction

Run:

timeout 20s ./os os_1_singleCPU_mlq_paging

### Actual Result

- Program crashes with SIGSEGV.
- Exit code: 139.
- Crash location: src/libmem.c:644.

### GDB Evidence

cur_vma = 0x0
caller->mm->mmap = 0x0

### Analysis

get_free_vmrg_area() dereferences a NULL cur_vma.

The incomplete initialization of mm->mmap
in init_mm() is a suspected upstream cause.

### Expected Result

Memory operations execute without invalid
pointer access, and the simulator progresses
through the configured workload.

### Suggested Owner

Memory Management team.

---

## BUG-003 — CPU Idle Infinite Loop

**Test:** SCHED-005
**Severity:** High
**Status:** Open

### Reproduction

Run:

timeout 20s ./os os_test_idle

Configuration:
- 2 CPUs
- 1 process
- Process arrival: time slot 5

### Actual Result

- CPU 1 completes process 1 and stops.
- CPU 0 does not report stopping.
- Timer continues advancing.
- Exit code: 124 (timeout).

### Analysis

In src/os.c, cpu_routine() has an early
continue when get_proc() returns NULL.

This bypasses the later completion check.

### Expected Result

Both CPUs stop after the loader has completed
and no runnable processes remain.

### Suggested Owner

OS execution / CPU lifecycle integration.

---

## Notes

These findings were reproduced locally.

No implementation fixes are included in
this report. Both bugs require regression
testing after their respective fixes.
