#ifndef OSSIM_KERNEL_MEMORY_H
#define OSSIM_KERNEL_MEMORY_H
#include "common.h"
struct sc_regs;
int kernel_sys_memmap(struct krnl_t *, uint32_t, struct sc_regs *);
int kernel_memory_init(struct krnl_t *);
void kernel_memory_destroy(struct krnl_t *);
int kernel_memory_alloc(struct pcb_t *, uint32_t region, addr_t size, addr_t *out);
int kernel_memory_free(struct pcb_t *, uint32_t region);
int kernel_memory_release_process(struct krnl_t *, uint32_t pid);
int kernel_memory_has_region(struct pcb_t *, uint32_t region);
int kernel_memory_byte(struct pcb_t *, uint32_t region, addr_t offset,
                       BYTE *value, int write);
int MEMPHY_take_contiguous(struct memphy_struct *, addr_t page_count,
                           struct framephy_struct **frames);
void MEMPHY_return_frames(struct memphy_struct *, struct framephy_struct *);
#endif
