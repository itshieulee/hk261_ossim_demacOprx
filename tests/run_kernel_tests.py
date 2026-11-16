import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
build = root / "build" / "kernel"
build.mkdir(parents=True, exist_ok=True)
table = []
for line in (root / "src" / "syscall.tbl").read_text().splitlines():
    fields = line.split()
    if fields and fields[0].isdigit():
        table.append(f"__SYSCALL({fields[0]}, {fields[2]})")
(build / "syscalltbl.lst").write_text("\n".join(table) + "\n")
sources = ["process", "kernel_memory", "kernel_frames", "mm-memphy", "kernel_syscall", "sys_mem", "libmem",
           "libstd", "syscall", "sys_listsyscall", "mm64", "mm", "mm-vm"]
executable = build / ("kernel_memory_test.exe" if os.name == "nt" else "kernel_memory_test")
compiler = os.environ.get("CC", "gcc")
command = [compiler, "-std=gnu11", "-g", "-Iinclude", "-Ibuild/kernel",
           "-ffunction-sections", "-fdata-sections"]
command += [f"src/{name}.c" for name in sources]
command += ["tests/kernel_memory_test.c", "-Wl,--gc-sections", "-pthread",
            "-o", str(executable)]
try:
    subprocess.run(command, cwd=root, check=True, timeout=90)
    subprocess.run([str(executable)], cwd=root, check=True, timeout=15)
except (subprocess.CalledProcessError, subprocess.TimeoutExpired, OSError) as error:
    print(f"FAIL: {error}", file=sys.stderr)
    sys.exit(1)
