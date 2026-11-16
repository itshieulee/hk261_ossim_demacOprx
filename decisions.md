# Week 1 decisions
## W1-D01: Preserve the provided template
- Decisions: Only insert code at existing TODO placeholders. Keep original
  headers, function signatures, statements, comments and build/lifecycle unchanged.
- Why: The assignment template and other members' integration ownership must be
  preserved. The user's latest instruction takes precedence over earlier edits.
- Rejected: Changing `struct krnl_t`, Makefile, loader/exit or the syscall dispatcher
  automatically to make an end-to-end demo work.

## W1-D02: Keep kernel functionality in separate modules
- Decisions: Add process registry, kernel allocator, frame reservation and memmap
  handler modules. Store kernel state externally, keyed by kernel identity.
- Why: This keeps implementation possible without modifying original structures.
- Rejected: A shared user mapping for every process, fake PCBs, and a standalone
  memory bank unrelated to the OS RAM.

## W1-D03: Hold process references across handlers
- Decisions: Lookup by kernel/PID; reject new references during exit and drain
  outstanding references before reclaim. Leave loader/exit hooks pending agreement.
- Why: A PCB must remain alive after lookup, independent of queue transitions.
- Rejected: Searching ready queues only, borrowing an unprotected pointer, or
  freeing a referenced PCB.

## W1-D04: Reserve complete contiguous kernel allocations
- Decisions: Validate PID/region ownership and bounds; reserve whole 4 KiB frames
  atomically and return retained nodes on free. Use a provisional kernel direct map.
- Why: Failed allocations must not leak frames; kernel addresses are simulated
  addresses rather than host pointers.
- Rejected: Partial-success allocation, replacing the page walker, or claiming
  the new frame lock already protects unchanged legacy allocators.
