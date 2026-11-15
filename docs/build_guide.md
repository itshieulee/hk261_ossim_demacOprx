### Environment setup (Ubuntu 24.04)

The DMA testbench is built against the Verilator 5.x runtime.
Ubuntu 24.04 provides Verilator 5.x directly from apt:

```bash
sudo apt update
sudo apt install -y build-essential verilator git
verilator --version        # expected: Verilator 5.x
```

### Testbench arguments

```bash
./dma_testbench [source_offset] [destination_offset] [length]
```

- Offsets are byte positions inside a 1 MB simulated memory bank (bank 0).
- `length` is the number of bytes to copy.
- Defaults: `0x1000 0x2000 512`.
- `destination_offset + length + 8` must stay inside the 1 MB bank.

### Reading the result

After the DMA transfer, the testbench checks two things:

- **data**: the `length` bytes at the destination must match the source.
- **guard**: the 8 bytes right after the destination are pre-filled with
  `0xAA` and must stay unchanged (detects writes past the requested length).

```
VERIFY data : PASS (0 errors, first bad byte = -1)
VERIFY guard: PASS (0 of 8 guard bytes overwritten)
```

The exit code is `0` if all checks pass, `1` otherwise:

```bash
./dma_testbench 0x1000 0x2000 16 ; echo "exit=$?"
```

### Known behaviour

- The DMA engine transfers data in 4-byte words and rounds the length up.
  If `length` is not a multiple of 4, up to 3 bytes after the destination
  are overwritten, so the guard check fails, e.g. `length = 5`:
  ```
  VERIFY guard: FAIL (3 of 8 guard bytes overwritten)
  ```
  This is a known limitation and will be handled in a later PR.

