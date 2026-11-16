#include "common.h"
#include "syscall.h"
#include "process.h"
#include "kernel_memory.h"
#include "libmem.h"
#include <errno.h>
int kernel_sys_memmap(struct krnl_t *kernel, uint32_t pid, struct sc_regs *regs)
{
    if (!kernel || !regs) return -EINVAL;
    struct process_ref *ref;
    int rc = process_acquire(kernel, pid, &ref);
    if (rc) return rc;
    struct pcb_t *caller = process_context(ref);
    switch (regs->a1) {
    case 6:
        if (regs->a3 >= PAGING_MAX_SYMTBL_SZ) rc = -EINVAL;
        else {
            addr_t address;
            rc = kernel_memory_alloc(caller, (uint32_t)regs->a3, regs->a2, &address);
            if (!rc) regs->a4 = address;
        }
        break;
    case 7:
        rc = regs->a2 >= PAGING_MAX_SYMTBL_SZ ? -EINVAL :
             kernel_memory_free(caller, (uint32_t)regs->a2);
        break;
    case SYSMEM_IO_READ:
    case SYSMEM_IO_WRITE:
        rc = -EACCES;
        break;
    case SYSMEM_MAP_OP:
    case SYSMEM_INC_OP:
    case SYSMEM_SWP_OP:
        rc = -ENOSYS;
        break;
    default:
        rc = -EINVAL;
    }
    process_release(ref);
    return rc;
}
