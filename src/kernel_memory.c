#include "kernel_memory.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define KERNEL_PAGE_SIZE UINT64_C(4096)
#define KERNEL_VA_BASE UINT64_C(0xff11000000000000)

struct kernel_region {
    uint32_t pid, id;
    addr_t size, phys_addr;
    struct framephy_struct *frames;
    struct kernel_region *next;
};
struct kernel_memory {
    pthread_mutex_t lock;
    struct kernel_region *regions;
    struct krnl_t *kernel;
    struct kernel_memory *next;
};

static struct kernel_memory *memories;
static pthread_mutex_t memories_lock = PTHREAD_MUTEX_INITIALIZER;

static struct kernel_memory *memory_for(struct krnl_t *kernel)
{
    pthread_mutex_lock(&memories_lock);
    struct kernel_memory *it = memories;
    while (it && it->kernel != kernel) it = it->next;
    pthread_mutex_unlock(&memories_lock);
    return it;
}

static struct kernel_region *find_region(struct kernel_memory *memory,
                                          uint32_t pid, uint32_t id)
{
    for (struct kernel_region *it = memory->regions; it; it = it->next)
        if (it->pid == pid && it->id == id) return it;
    return NULL;
}

int kernel_memory_init(struct krnl_t *kernel)
{
    if (!kernel || !kernel->mram || !kernel->mram->storage ||
        kernel->mram->maxsz <= 0) return -EINVAL;
    if (memory_for(kernel)) return -EALREADY;
    struct kernel_memory *memory = calloc(1, sizeof(*memory));
    if (!memory) return -ENOMEM;
    int rc = pthread_mutex_init(&memory->lock, NULL);
    if (rc) { free(memory); return -rc; }
    memory->kernel = kernel;
    pthread_mutex_lock(&memories_lock);
    for (struct kernel_memory *it = memories; it; it = it->next) {
        if (it->kernel == kernel) {
            pthread_mutex_unlock(&memories_lock);
            pthread_mutex_destroy(&memory->lock);
            free(memory);
            return -EALREADY;
        }
    }
    memory->next = memories;
    memories = memory;
    pthread_mutex_unlock(&memories_lock);
    return 0;
}

int kernel_memory_alloc(struct pcb_t *process, uint32_t id,
                         addr_t size, addr_t *out)
{
    if (!process || !process->krnl || !out || !size ||
        id >= PAGING_MAX_SYMTBL_SZ) return -EINVAL;
    struct krnl_t *kernel = process->krnl;
    struct kernel_memory *memory = memory_for(kernel);
    if (!memory) {
        int rc = kernel_memory_init(kernel);
        if (rc && rc != -EALREADY) return rc;
        memory = memory_for(kernel);
    }
    if (!memory || !kernel->mram) return -EINVAL;
    if (size > (addr_t)kernel->mram->maxsz) return -ENOMEM;
    addr_t count = size / KERNEL_PAGE_SIZE + (size % KERNEL_PAGE_SIZE != 0);
    struct kernel_region *region = calloc(1, sizeof(*region));
    if (!region) return -ENOMEM;
    pthread_mutex_lock(&memory->lock);
    if (find_region(memory, process->pid, id) || (process->mm &&
        process->mm->symrgtbl[id].rg_end > process->mm->symrgtbl[id].rg_start)) {
        pthread_mutex_unlock(&memory->lock);
        free(region);
        return -EEXIST;
    }
    int rc = MEMPHY_take_contiguous(kernel->mram, count, &region->frames);
    if (rc) {
        pthread_mutex_unlock(&memory->lock);
        free(region);
        return rc;
    }
    region->pid = process->pid;
    region->id = id;
    region->size = size;
    region->phys_addr = region->frames->fpn * KERNEL_PAGE_SIZE;
    memset(kernel->mram->storage + region->phys_addr, 0,
           (size_t)(count * KERNEL_PAGE_SIZE));
    region->next = memory->regions;
    memory->regions = region;
    *out = KERNEL_VA_BASE + region->phys_addr;
    pthread_mutex_unlock(&memory->lock);
    return 0;
}

static void reclaim(struct krnl_t *kernel, struct kernel_region *region)
{
    addr_t count = region->size / KERNEL_PAGE_SIZE +
                   (region->size % KERNEL_PAGE_SIZE != 0);
    memset(kernel->mram->storage + region->phys_addr, 0,
           (size_t)(count * KERNEL_PAGE_SIZE));
    MEMPHY_return_frames(kernel->mram, region->frames);
    free(region);
}

int kernel_memory_free(struct pcb_t *process, uint32_t id)
{
    if (!process || !process->krnl || id >= PAGING_MAX_SYMTBL_SZ ||
        !memory_for(process->krnl)) return -EINVAL;
    struct kernel_memory *memory = memory_for(process->krnl);
    pthread_mutex_lock(&memory->lock);
    struct kernel_region **link = &memory->regions;
    while (*link && ((*link)->pid != process->pid || (*link)->id != id))
        link = &(*link)->next;
    if (!*link) { pthread_mutex_unlock(&memory->lock); return -ENOENT; }
    struct kernel_region *region = *link;
    *link = region->next;
    reclaim(process->krnl, region);
    pthread_mutex_unlock(&memory->lock);
    return 0;
}

int kernel_memory_has_region(struct pcb_t *process, uint32_t id)
{
    if (!process || !process->krnl || !memory_for(process->krnl) ||
        id >= PAGING_MAX_SYMTBL_SZ) return 0;
    struct kernel_memory *memory = memory_for(process->krnl);
    pthread_mutex_lock(&memory->lock);
    int present = find_region(memory, process->pid, id) != NULL;
    pthread_mutex_unlock(&memory->lock);
    return present;
}

int kernel_memory_byte(struct pcb_t *process, uint32_t id, addr_t offset,
                       BYTE *value, int write)
{
    if (!process || !process->krnl || !memory_for(process->krnl) ||
        !value || id >= PAGING_MAX_SYMTBL_SZ || (write != 0 && write != 1))
        return -EINVAL;
    struct kernel_memory *memory = memory_for(process->krnl);
    pthread_mutex_lock(&memory->lock);
    struct kernel_region *region = find_region(memory, process->pid, id);
    int rc = 0;
    if (!region) rc = -EACCES;
    else if (offset >= region->size) rc = -EFAULT;
    else if (write) process->krnl->mram->storage[region->phys_addr + offset] = *value;
    else *value = process->krnl->mram->storage[region->phys_addr + offset];
    pthread_mutex_unlock(&memory->lock);
    return rc;
}

int kernel_memory_release_process(struct krnl_t *kernel, uint32_t pid)
{
    if (!kernel || !memory_for(kernel)) return -EINVAL;
    struct kernel_memory *memory = memory_for(kernel);
    pthread_mutex_lock(&memory->lock);
    struct kernel_region **link = &memory->regions;
    while (*link) {
        struct kernel_region *region = *link;
        if (region->pid != pid) { link = &region->next; continue; }
        *link = region->next;
        reclaim(kernel, region);
    }
    pthread_mutex_unlock(&memory->lock);
    return 0;
}

void kernel_memory_destroy(struct krnl_t *kernel)
{
    if (!kernel || !memory_for(kernel)) return;
    struct kernel_memory *memory = memory_for(kernel);
    pthread_mutex_lock(&memory->lock);
    while (memory->regions) {
        struct kernel_region *region = memory->regions;
        memory->regions = region->next;
        reclaim(kernel, region);
    }
    pthread_mutex_unlock(&memory->lock);
    pthread_mutex_destroy(&memory->lock);
    pthread_mutex_lock(&memories_lock);
    struct kernel_memory **link = &memories;
    while (*link != memory) link = &(*link)->next;
    *link = memory->next;
    pthread_mutex_unlock(&memories_lock);
    free(memory);
}
