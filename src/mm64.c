/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

/*
 * PAGING based Memory Management
 * Memory management unit mm/mm.c
 */

/* Copyright (C) 2026 pdnguyen, HCMC University of Technology VNU-HCM.
 * CO2018 educational source. See the original project LICENSE.
 * Week 1: sparse five-level tables, frame-range mapping and VA -> PA.
 */
#include "mm64.h"
#include <inttypes.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#ifdef MM64
_Static_assert(sizeof(addr_t) == 8, "MM64 needs 64-bit entries");
_Static_assert(sizeof(uintptr_t) <= sizeof(addr_t), "host pointers must fit");
static pthread_mutex_t page_lock = PTHREAD_MUTEX_INITIALIZER;

int mm64_is_canonical(addr_t address)
{
    addr_t upper = address >> 57;
    return upper == (((address >> 56) & 1U) ? 0x7fU : 0U);
}

int get_pd_from_address(addr_t addr, addr_t *pgd, addr_t *p4d,
                        addr_t *pud, addr_t *pmd, addr_t *pt)
{
    if (!pgd || !p4d || !pud || !pmd || !pt || !mm64_is_canonical(addr))
        return -1;
    *pgd = PAGING64_ADDR_PGD(addr);
    *p4d = PAGING64_ADDR_P4D(addr);
    *pud = PAGING64_ADDR_PUD(addr);
    *pmd = PAGING64_ADDR_PMD(addr);
    *pt = PAGING64_ADDR_PT(addr);
    return 0;
}

int get_pd_from_pagenum(addr_t pgn, addr_t *pgd, addr_t *p4d,
                        addr_t *pud, addr_t *pmd, addr_t *pt)
{
    /* Reject overflow before shifting, including invalid sign-extension bits. */
    if (pgn > (UINT64_MAX >> MM64_PAGE_SHIFT))
        return -1;
    return get_pd_from_address(pgn << MM64_PAGE_SHIFT, pgd, p4d, pud, pmd, pt);
}

/* Every table has 512 uint64_t slots = 4096 bytes of payload.
 * Levels 5..2 store host pointers encoded as integers. Level 1 stores the
 * assignment's PTE value. These host pointers are NOT simulated physical
 * addresses, bus addresses, or DMA addresses.
 * Caller holds page_lock. Reads use create=0 and never allocate memory.
 */
static addr_t *walk_pte(struct mm_struct *mm, addr_t pgn, int create)
{
    addr_t index[5];
    addr_t *table;
    if (mm == NULL || get_pd_from_pagenum(pgn, &index[0], &index[1],
                                         &index[2], &index[3], &index[4]) != 0)
        return NULL;
    if (mm->pgd == NULL) {
        if (!create)
            return NULL;
        mm->pgd = calloc(MM64_ENTRIES, sizeof(*mm->pgd));
        if (mm->pgd == NULL)
            return NULL;
    }
    table = mm->pgd;
    for (unsigned level = 0; level < 4; ++level) {
        if (table[index[level]] == 0) {
            addr_t *child;
            if (!create)
                return NULL;
            child = calloc(MM64_ENTRIES, sizeof(*child));
            if (child == NULL)
                return NULL;
            table[index[level]] = (addr_t)(uintptr_t)child;
        }
        table = (addr_t *)(uintptr_t)table[index[level]];
    }
    return &table[index[4]];
}

/* Caller holds page_lock. Removed mappings must not remain FIFO victims. */
static void forget_fifo_page(struct mm_struct *mm, addr_t pgn)
{
    struct pgn_t **link = &mm->fifo_pgn;
    while (*link != NULL) {
        if ((*link)->pgn == pgn) {
            struct pgn_t *old = *link;
            *link = old->pg_next;
            free(old);
        } else {
            link = &(*link)->pg_next;
        }
    }
}

/* Preserve the supplied PTE bit format: resident frame in bits 0..12,
 * dirty at 28, swapped at 30, present at 31. A 64-bit slot does not require
 * changing that format to a real x86 hardware entry.
 */
int init_pte(addr_t *pte, int pre, addr_t fpn, int drt,
             int swp, int swptyp, addr_t swpoff)
{
    addr_t value = 0;
    if (pte == NULL)
        return -1;
    if (swp) {
        if (swptyp < 0 || swptyp >= PAGING_MAX_MMSWP ||
            swpoff > (addr_t)(PAGING_PTE_SWPOFF_MASK >> PAGING_PTE_SWPOFF_LOBIT))
            return -1;
        value = (addr_t)PAGING_PTE_SWAPPED_MASK |
                ((addr_t)swptyp << PAGING_PTE_SWPTYP_LOBIT) |
                (swpoff << PAGING_PTE_SWPOFF_LOBIT);
    } else if (pre) {
        if (fpn > (addr_t)(PAGING_PTE_FPN_MASK >> PAGING_PTE_FPN_LOBIT))
            return -1;
        value = (addr_t)PAGING_PTE_PRESENT_MASK | (fpn << PAGING_PTE_FPN_LOBIT);
    }
    if (drt && (pre || swp))
        value |= (addr_t)PAGING_PTE_DIRTY_MASK;
    *pte = value;
    return 0;
}

addr_t pte_get_entry(struct mm_struct *mm, addr_t pgn, struct pcb_t *caller)
{
    addr_t *slot, value;
    (void)caller;                 /* The supplied mm selects the address space. */
    pthread_mutex_lock(&page_lock);
    slot = walk_pte(mm, pgn, 0);
    value = slot == NULL ? 0 : *slot;
    pthread_mutex_unlock(&page_lock);
    return value;
}

int pte_set_entry(struct mm_struct *mm, addr_t pgn, uint64_t value,
                   struct pcb_t *caller)
{
    addr_t *slot;
    (void)caller;
    /* Generic setter accepts the assignment's 32-bit leaf format only. */
    if (value > UINT32_MAX ||
        ((value & PAGING_PTE_PRESENT_MASK) && (value & PAGING_PTE_SWAPPED_MASK)))
        return -1;
    pthread_mutex_lock(&page_lock);
    slot = walk_pte(mm, pgn, value != 0);
    if (slot != NULL) {
        *slot = value;
        if (value == 0)
            forget_fifo_page(mm, pgn);
    }
    pthread_mutex_unlock(&page_lock);
    /* Clearing a missing valid page is a successful no-op. */
    if (slot == NULL && value == 0) {
        addr_t a, b, c, d, e;
        return mm != NULL && get_pd_from_pagenum(pgn, &a, &b, &c, &d, &e) == 0
               ? 0 : -1;
    }
    return slot != NULL ? 0 : -1;
}

int pte_set_fpn(struct mm_struct *mm, addr_t pgn, addr_t fpn,
                 struct pcb_t *caller)
{
    addr_t value;
    if (init_pte(&value, 1, fpn, 0, 0, 0, 0) != 0)
        return -1;
    return pte_set_entry(mm, pgn, value, caller);
}

/* Updates metadata only; actual swap I/O is a later memory-module operation. */
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff)
{
    addr_t *slot, value;
    if (caller == NULL || init_pte(&value, 0, 0, 0, 1, swptyp, swpoff) != 0)
        return -1;
    pthread_mutex_lock(&page_lock);
    slot = walk_pte(caller->mm, pgn, 0);
    if (slot == NULL || *slot == 0) {
        pthread_mutex_unlock(&page_lock);
        return -1;
    }
    *slot = value;
    pthread_mutex_unlock(&page_lock);
    return 0;
}

int mm64_translate(struct mm_struct *mm, addr_t va, addr_t *pa)
{
    addr_t entry;
    if (pa == NULL || !mm64_is_canonical(va))
        return -1;
    entry = pte_get_entry(mm, va >> MM64_PAGE_SHIFT, NULL);
    if (!(entry & PAGING_PTE_PRESENT_MASK) || (entry & PAGING_PTE_SWAPPED_MASK))
        return -1;
    *pa = (addr_t)PAGING_PTE_FPN(entry) * PAGING64_PAGESZ + PAGING64_ADDR_OFFST(va);
    return 0;
}

/* Remove a resident mapping and return its frame number; caller releases the
 * frame with MEMPHY_put_freefp. No hidden frame ownership change here.
 */
int mm64_unmap(struct mm_struct *mm, addr_t va, addr_t *old_fpn)
{
    addr_t *slot;
    if (old_fpn == NULL || !mm64_is_canonical(va))
        return -1;
    pthread_mutex_lock(&page_lock);
    slot = walk_pte(mm, va >> MM64_PAGE_SHIFT, 0);
    if (slot == NULL || !(*slot & PAGING_PTE_PRESENT_MASK) ||
        (*slot & PAGING_PTE_SWAPPED_MASK)) {
        pthread_mutex_unlock(&page_lock);
        return -1;
    }
    *old_fpn = PAGING_PTE_FPN(*slot);
    *slot = 0;
    forget_fifo_page(mm, va >> MM64_PAGE_SHIFT);
    pthread_mutex_unlock(&page_lock);
    return 0;
}

int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
    struct vm_area_struct *vma;
    (void)caller;                 /* Also valid for init_mm(os.mm, NULL). */
    if (mm == NULL)
        return -1;
    memset(mm, 0, sizeof(*mm));
    mm->pgd = calloc(MM64_ENTRIES, sizeof(*mm->pgd));
    vma = calloc(1, sizeof(*vma));
    if (mm->pgd == NULL || vma == NULL) {
        free(mm->pgd);
        free(vma);
        mm->pgd = NULL;
        return -1;
    }
    /* Empty heap: no zero-length free-region node is needed. */
    vma->vm_mm = mm;
    mm->mmap = vma;
    return 0;
}

static size_t count_tables(addr_t *table, unsigned level)
{
    size_t count = table != NULL;
    if (table != NULL && level > 1)
        for (unsigned i = 0; i < MM64_ENTRIES; ++i)
            if (table[i] != 0)
                count += count_tables((addr_t *)(uintptr_t)table[i], level - 1);
    return count;
}

size_t mm64_table_count(struct mm_struct *mm)
{
    size_t count;
    pthread_mutex_lock(&page_lock);
    count = mm == NULL ? 0 : count_tables(mm->pgd, 5);
    pthread_mutex_unlock(&page_lock);
    return count;
}

static void destroy_tables(addr_t *table, unsigned level)
{
    if (table == NULL)
        return;
    if (level > 1)
        for (unsigned i = 0; i < MM64_ENTRIES; ++i)
            if (table[i] != 0)
                destroy_tables((addr_t *)(uintptr_t)table[i], level - 1);
    free(table);
}

/* Frees owned table/VMA/FIFO metadata. Frames, SWAP, and a later kernel cache
 * pool have their own lifecycles. Stop users of this address space first.
 */
void mm64_destroy(struct mm_struct *mm)
{
    if (mm == NULL)
        return;
    pthread_mutex_lock(&page_lock);
    destroy_tables(mm->pgd, 5);
    while (mm->mmap != NULL) {
        struct vm_area_struct *next = mm->mmap->vm_next;
        struct vm_rg_struct *rg = mm->mmap->vm_freerg_list;
        while (rg != NULL) {
            struct vm_rg_struct *rg_next = rg->rg_next;
            free(rg);
            rg = rg_next;
        }
        free(mm->mmap);
        mm->mmap = next;
    }
    while (mm->fifo_pgn != NULL) {
        struct pgn_t *next = mm->fifo_pgn->pg_next;
        free(mm->fifo_pgn);
        mm->fifo_pgn = next;
    }
    memset(mm, 0, sizeof(*mm));
    pthread_mutex_unlock(&page_lock);
}

/* A range is page-aligned, nonempty, canonical and does not wrap. */
static int valid_range(addr_t start, int count, addr_t *end)
{
    addr_t bytes;
    if (count <= 0 || (start & (PAGING64_PAGESZ - 1)) != 0)
        return -1;
    bytes = (addr_t)count * PAGING64_PAGESZ;
    if (start > UINT64_MAX - bytes || !mm64_is_canonical(start) ||
        !mm64_is_canonical(start + bytes - 1))
        return -1;
    *end = start + bytes;
    return 0;
}

/* Free only the temporary list returned by alloc_pages_range. */
static void discard_frame_list(struct framephy_struct *node,
                                struct memphy_struct *ram, int release)
{
    while (node != NULL) {
        struct framephy_struct *next = node->fp_next;
        if (release)
            MEMPHY_put_freefp(ram, node->fpn);
        free(node);
        node = next;
    }
}

int alloc_pages_range(struct pcb_t *caller, int count, struct framephy_struct **out)
{
    struct framephy_struct *head = NULL;
    struct framephy_struct **tail = &head;
    if (out == NULL)
        return -1;
    *out = NULL;
    if (caller == NULL || caller->krnl == NULL || caller->krnl->mram == NULL ||
        caller->mm == NULL || count <= 0)
        return -1;
    for (int i = 0; i < count; ++i) {
        struct framephy_struct *node = calloc(1, sizeof(*node));
        if (node == NULL) {
            discard_frame_list(head, caller->krnl->mram, 1);
            return -1;
        }
        if (MEMPHY_get_freefp(caller->krnl->mram, &node->fpn) != 0) {
            free(node);
            discard_frame_list(head, caller->krnl->mram, 1);
            return -3000;        /* Original caller's out-of-memory status. */
        }
        node->owner = caller->mm;
        *tail = node;
        tail = &node->fp_next;
    }
    *out = head;
    return 0;
}

int vmap_page_range(struct pcb_t *caller, addr_t addr, int count,
                    struct framephy_struct *frames, struct vm_rg_struct *result)
{
    addr_t end;
    struct framephy_struct *fp = frames;
    struct pgn_t *pending = NULL;
    if (caller == NULL || caller->mm == NULL || result == NULL ||
        valid_range(addr, count, &end) != 0)
        return -1;
    pthread_mutex_lock(&page_lock);
    /* Preflight all memory allocations/targets before writing any PTEs. */
    for (int i = 0; i < count; ++i) {
        addr_t *slot, value;
        struct pgn_t *pg;
        if (fp == NULL || init_pte(&value, 1, fp->fpn, 0, 0, 0, 0) != 0)
            goto fail;
        slot = walk_pte(caller->mm, (addr >> MM64_PAGE_SHIFT) + (addr_t)i, 1);
        if (slot == NULL || *slot != 0)
            goto fail;
        pg = calloc(1, sizeof(*pg));
        if (pg == NULL)
            goto fail;
        pg->pgn = (addr >> MM64_PAGE_SHIFT) + (addr_t)i;
        pg->pg_next = pending;
        pending = pg;
        fp = fp->fp_next;
    }
    fp = frames;
    for (int i = 0; i < count; ++i) {
        addr_t *slot = walk_pte(caller->mm,
                               (addr >> MM64_PAGE_SHIFT) + (addr_t)i, 0);
        init_pte(slot, 1, fp->fpn, 0, 0, 0, 0);
        fp = fp->fp_next;
    }
    /* New pages go at the front; the supplied victim policy takes the tail. */
    if (pending != NULL) {
        struct pgn_t *tail = pending;
        while (tail->pg_next != NULL)
            tail = tail->pg_next;
        tail->pg_next = caller->mm->fifo_pgn;
        caller->mm->fifo_pgn = pending;
    }
    result->rg_start = addr;
    result->rg_end = end;         /* Half-open interval [start, end). */
    result->vmaid = 0;
    result->rg_next = NULL;
    pthread_mutex_unlock(&page_lock);
    return 0;
fail:
    while (pending != NULL) {
        struct pgn_t *next = pending->pg_next;
        free(pending);
        pending = next;
    }
    /* Empty tables created during preflight remain reusable until destroy. */
    pthread_mutex_unlock(&page_lock);
    return -1;
}

int vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend,
                addr_t mapstart, int count, struct vm_rg_struct *result)
{
    struct framephy_struct *frames = NULL;
    addr_t end;
    int status;
    if (result == NULL || valid_range(mapstart, count, &end) != 0 ||
        mapstart < astart || end > aend)
        return -1;
    status = alloc_pages_range(caller, count, &frames);
    if (status != 0)
        return status;
    status = vmap_page_range(caller, mapstart, count, frames, result);
    discard_frame_list(frames, caller->krnl->mram, status != 0);
    return status;
}

/* Clear PTE metadata over a range. Does not create tables or release frames. */
int vmap_pgd_memset(struct pcb_t *caller, addr_t addr, int count)
{
    addr_t end;
    if (caller == NULL || caller->mm == NULL || valid_range(addr, count, &end) != 0)
        return -1;
    pthread_mutex_lock(&page_lock);
    for (int i = 0; i < count; ++i) {
        addr_t *slot = walk_pte(caller->mm, (addr >> MM64_PAGE_SHIFT) + (addr_t)i, 0);
        if (slot != NULL) {
            *slot = 0;
            forget_fifo_page(caller->mm, (addr >> MM64_PAGE_SHIFT) + (addr_t)i);
        }
    }
    pthread_mutex_unlock(&page_lock);
    return 0;
}

int __swap_cp_page(struct memphy_struct *source, addr_t srcfpn,
                    struct memphy_struct *dest, addr_t dstfpn)
{
    if (source == NULL || dest == NULL || source->maxsz < PAGING64_PAGESZ ||
        dest->maxsz < PAGING64_PAGESZ ||
        srcfpn >= (addr_t)(source->maxsz / PAGING64_PAGESZ) ||
        dstfpn >= (addr_t)(dest->maxsz / PAGING64_PAGESZ))
        return -1;
    for (addr_t i = 0; i < PAGING64_PAGESZ; ++i) {
        BYTE value;
        if (MEMPHY_read(source, srcfpn * PAGING64_PAGESZ + i, &value) != 0 ||
            MEMPHY_write(dest, dstfpn * PAGING64_PAGESZ + i, value) != 0)
            return -1;
    }
    return 0;
}

struct vm_rg_struct *init_vm_rg(addr_t start, addr_t end)
{
    struct vm_rg_struct *node;
    if (end < start)
        return NULL;
    node = calloc(1, sizeof(*node));
    if (node != NULL) {
        node->rg_start = start;
        node->rg_end = end;
    }
    return node;
}

int enlist_vm_rg_node(struct vm_rg_struct **list, struct vm_rg_struct *node)
{
    if (list == NULL || node == NULL)
        return -1;
    node->rg_next = *list;
    *list = node;
    return 0;
}

int enlist_pgn_node(struct pgn_t **list, addr_t pgn)
{
    struct pgn_t *node;
    if (list == NULL)
        return -1;
    node = calloc(1, sizeof(*node));
    if (node == NULL)
        return -1;
    node->pgn = pgn;
    node->pg_next = *list;
    *list = node;
    return 0;
}

int print_list_fp(struct framephy_struct *node)
{
    for (; node != NULL; node = node->fp_next)
        printf("frame=%" PRIu64 "\n", (uint64_t)node->fpn);
    return 0;
}

int print_list_rg(struct vm_rg_struct *node)
{
    for (; node != NULL; node = node->rg_next)
        printf("region=[0x%" PRIx64 ",0x%" PRIx64 ")\n",
               (uint64_t)node->rg_start, (uint64_t)node->rg_end);
    return 0;
}

int print_list_vma(struct vm_area_struct *node)
{
    for (; node != NULL; node = node->vm_next)
        printf("vma=%lu [0x%" PRIx64 ",0x%" PRIx64 ")\n", node->vm_id,
               (uint64_t)node->vm_start, (uint64_t)node->vm_end);
    return 0;
}

int print_list_pgn(struct pgn_t *node)
{
    for (; node != NULL; node = node->pg_next)
        printf("page=%" PRIu64 "\n", (uint64_t)node->pgn);
    return 0;
}

int print_pgtbl(struct pcb_t *caller, addr_t start, addr_t end)
{
    addr_t page;
    if (caller == NULL || caller->mm == NULL || start > end ||
        !mm64_is_canonical(start) || (end != start && !mm64_is_canonical(end - 1)))
        return -1;
    page = start & ~((addr_t)PAGING64_PAGESZ - 1);
    while (page < end) {
        addr_t value = pte_get_entry(caller->mm, page >> MM64_PAGE_SHIFT, caller);
        if (value != 0)
            printf("VA=0x%016" PRIx64 " PTE=0x%08" PRIx64 "\n",
                   (uint64_t)page, (uint64_t)value);
        if (page > UINT64_MAX - PAGING64_PAGESZ)
            break;
        page += PAGING64_PAGESZ;
    }
    return 0;
}
#endif
