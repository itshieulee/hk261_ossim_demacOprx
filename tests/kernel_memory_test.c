#include "common.h"
#include "process.h"
#include "kernel_memory.h"
#include "mm.h"
#include "libmem.h"
#include "syscall.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
enum { SYSMEM_KERNEL_ALLOC = 6 };
static atomic_int started, finished;
static struct krnl_t *exiting_kernel;
static void *exit_thread(void *unused) {
    (void)unused;
    atomic_store(&started, 1);
    assert(process_begin_exit(exiting_kernel, 1) == 0);
    atomic_store(&finished, 1);
    return NULL;
}
static int count_free(struct memphy_struct *ram) {
    int n = 0;
    for (struct framephy_struct *it = ram->free_fp_list; it; it = it->fp_next) ++n;
    return n;
}
int main(void) {
    struct memphy_struct ram;
    struct krnl_t kernel = {0}, wrong_kernel = {0};
    struct mm_struct mm = {0}, mm2 = {0};
    struct pcb_t p = {0}, q = {0};
    ram = (struct memphy_struct){0};
    ram.storage = calloc(32768, 1); ram.maxsz = 32768; ram.rdmflg = 1;
    assert(ram.storage);
    for (int i = 7; i >= 0; --i) {
        struct framephy_struct *node = calloc(1, sizeof(*node));
        assert(node); node->fpn = (addr_t)i;
        node->fp_next = ram.free_fp_list; ram.free_fp_list = node;
    }
    kernel.mram = &ram;
    p.krnl = &kernel; p.pid = 1; p.mm = &mm;
    q.krnl = &kernel; q.pid = 2; q.mm = &mm2;
    assert(kernel_memory_init(&kernel) == 0);
    assert(kernel_memory_init(&kernel) == -EALREADY);
    assert(process_register(&kernel, &p) == 0);
    assert(process_register(&kernel, &p) == -EEXIST);
    assert(process_register(&kernel, &q) == 0);
    struct process_ref *ref = NULL;
    assert(process_acquire(&wrong_kernel, 1, &ref) == -ESRCH && ref == NULL);
    assert(libkmem_malloc(&p, 5000, 0) == 0 && count_free(&ram) == 6);
    assert(libkmem_malloc(&p, 1, 0) == -EEXIST);
    assert(libkmem_malloc(&p, 0, 1) == -EINVAL);
    assert(libkmem_malloc(&p, 1, PAGING_MAX_SYMTBL_SZ) == -EINVAL);
    addr_t address = 123;
    assert(__kmalloc(&p, 0, 1, 4096, &address) == 0);
    assert(address > UINT64_C(0x100000000));
    BYTE b = 0;
    assert(__read_kernel_mem(&p, 0, 0, 4096, &b) == 0 && b == 0);
    assert(__write_kernel_mem(&p, 0, 0, 4999, (BYTE)0xa5) == 0);
    assert(__read_kernel_mem(&p, 0, 0, 4999, &b) == 0 && (unsigned char)b == 0xa5);
    assert(__write_kernel_mem(&p, 0, 0, 5000, 1) == -EFAULT);
    assert(__write_kernel_mem(&p, 0, 0, UINT64_MAX, 1) == -EFAULT);
    assert(__read_kernel_mem(&q, 0, 0, 0, &b) == -EACCES);
    assert(__read_user_mem(&p, 0, 0, 0, &b) == -1);
    assert(__write_user_mem(&p, 0, 0, 0, 1) == -1);
    struct sc_regs regs = {0}; regs.a1 = SYSMEM_IO_WRITE;
    assert(_syscall(&kernel, 1, 17, &regs) == -EACCES);
    assert(_syscall(&kernel, 99, 17, &regs) == -ESRCH);
    assert(kernel_sys_memmap(&kernel, 1, NULL) == -EINVAL);
    regs.a1 = UINT64_MAX;
    assert(_syscall(&kernel, 1, 17, &regs) == -EINVAL);
    regs.a1 = SYSMEM_KERNEL_ALLOC; regs.a2 = 1;
    regs.a3 = UINT64_C(0x100000000); regs.a4 = 777;
    assert(_syscall(&kernel, 1, 17, &regs) == -EINVAL && regs.a4 == 777);
    regs.a3 = 2; regs.a2 = UINT64_MAX;
    assert(_syscall(&kernel, 1, 17, &regs) == -ENOMEM);
    assert(regs.a4 == 777 && count_free(&ram) == 5);
    assert(kernel_memory_free(&p, 0) == 0);
    assert(libkmem_malloc(&p, 5000, 0) == 0);
    assert(__read_kernel_mem(&p, 0, 0, 4999, &b) == 0 && b == 0);
    assert(kernel_memory_release_process(&kernel, 1) == 0 && count_free(&ram) == 8);
    mm.symrgtbl[3].rg_start = UINT64_C(0x100000000);
    mm.symrgtbl[3].rg_end = UINT64_C(0x100001000);
    assert(libkmem_malloc(&p, 1, 3) == -EEXIST);
    assert(__read_user_mem(&p, 0, 3, 0, &b) == -1);
    struct framephy_struct *held[8];
    for (int i = 0; i < 8; ++i) assert(MEMPHY_take_contiguous(&ram, 1, &held[i]) == 0);
    for (int i = 0; i < 8; ++i) {
        if (held[i]->fpn % 2 == 0) {
            MEMPHY_return_frames(&ram, held[i]); held[i] = NULL;
        }
    }
    address = 888;
    assert(kernel_memory_alloc(&p, 4, 4097, &address) == -ENOMEM);
    assert(address == 888 && count_free(&ram) == 4);
    for (int i = 0; i < 8; ++i) {
        if (held[i] && held[i]->fpn == 1) {
            MEMPHY_return_frames(&ram, held[i]); held[i] = NULL; break;
        }
    }
    assert(kernel_memory_alloc(&p, 4, 4097, &address) == 0 && count_free(&ram) == 3);
    assert(kernel_memory_free(&p, 4) == 0);
    for (int i = 0; i < 8; ++i) if (held[i]) MEMPHY_return_frames(&ram, held[i]);
    assert(count_free(&ram) == 8);
    assert(process_acquire(&kernel, 1, &ref) == 0);
    assert(process_context(ref) == &p);
    exiting_kernel = &kernel;
    pthread_t thread;
    assert(pthread_create(&thread, NULL, exit_thread, NULL) == 0);
    while (!atomic_load(&started)) { }
    struct process_ref *probe;
    int rc;
    do { rc = process_acquire(&kernel, 1, &probe); if (!rc) process_release(probe); } while (!rc);
    assert(rc == -ESRCH && !atomic_load(&finished));
    assert(process_release(ref) == 0);
    assert(pthread_join(thread, NULL) == 0 && atomic_load(&finished));
    assert(process_begin_exit(&kernel, 2) == 0);
    kernel_memory_destroy(&kernel);
    while (ram.free_fp_list) {
        struct framephy_struct *next = ram.free_fp_list->fp_next;
        free(ram.free_fp_list); ram.free_fp_list = next;
    }
    free(ram.storage);
    puts("PASS: W1 modules and TODO entry points; full OS integration is pending");
    return 0;
}
