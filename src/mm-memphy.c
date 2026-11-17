/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

// #ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Memory physical module mm/mm-memphy.c
 */
#include "mm64.h"
#include <limits.h>
#include <pthread>
#include <stdlib.h>
#include <string.h>

/* Shared by RAM/SWAP devices. Coarse locking keeps the existing struct ABI. */
static pthread_mutex_t memphy_lock = PTHREAD_MUTEX_INITIALIZER;

static int frame_size(void)
{
#ifdef MM64
    return PAGING64_PAGESZ;
#else
    return PAGING_PAGESZ;
#endif
}

static void destroy_frame_list(struct framephy_struct *node)
{
    while (node != NULL) {
        struct framephy_struct *next = node->fp_next;
        free(node);
        node = next;
    }
}

/* Caller holds memphy_lock. Build the replacement before discarding old data. */
static int format_locked(struct memphy_struct *mp, int pagesz)
{
    struct framephy_struct *head = NULL;
    struct framephy_struct **tail = &head;
    int count;
    if (pagesz != frame_size() || mp->maxsz < 0 || mp->used_fp_list != NULL)
        return -1;
    count = mp->maxsz / pagesz;
    for (int i = 0; i < count; ++i) {
        struct framephy_struct *node = calloc(1, sizeof(*node));
        if (node == NULL) {
            destroy_frame_list(head);
            return -1;
        }
        node->fpn = (addr_t)i;     /* Frame zero is valid. */
        *tail = node;
        tail = &node->fp_next;
    }
    destroy_frame_list(mp->free_fp_list);
    mp->free_fp_list = head;
    return 0;
}

int MEMPHY_format(struct memphy_struct *mp, int pagesz)
{
    int result;
    if (mp == NULL)
        return -1;
    pthread_mutex_lock(&memphy_lock);
    result = format_locked(mp, pagesz);
    pthread_mutex_unlock(&memphy_lock);
    return result;
}

/* Initialize a fresh object; destroy before reinitializing.
 * Zero-sized SWAP banks occur in the supplied inputs and are valid.
 */
int init_memphy(struct memphy_struct *mp, addr_t max_size, int randomflg)
{
    if (mp == NULL)
        return -1;
    memset(mp, 0, sizeof(*mp));
    if (max_size > INT_MAX || (max_size != 0 && max_size < (addr_t)frame_size()))
        return -1;                /* The supplied maxsz field has type int. */
    mp->maxsz = (int)max_size;
    mp->rdmflg = randomflg != 0;
    if (max_size != 0) {
        mp->storage = calloc((size_t)max_size, sizeof(*mp->storage));
        if (mp->storage == NULL)
            goto fail;
    }
    if (MEMPHY_format(mp, frame_size()) != 0)
        goto fail;
    return 0;
fail:
    MEMPHY_destroy(mp);
    return -1;
}

int MEMPHY_get_freefp(struct memphy_struct *mp, addr_t *retfpn)
{
    if (mp == NULL || retfpn == NULL)
        return -1;
    pthread_mutex_lock(&memphy_lock);

    struct framephy_struct *node;
    node = mp->free_fp_list;
    if (node == NULL) {
        pthread_mutex_unlock(&memphy_lock);
        return -1;
    }
    mp->free_fp_list = node->fp_next;
    *retfpn = node->fpn;
    /* Reuse this node on release; no allocation can fail during free. */
    node->fp_next = mp->used_fp_list;
    mp->used_fp_list = node;
    pthread_mutex_unlock(&memphy_lock);
    return 0;
}

int MEMPHY_put_freefp(struct memphy_struct *mp, addr_t fpn)
{
    struct framephy_struct **link;
    struct framephy_struct *node;
    if (mp == NULL)
        return -1;
    pthread_mutex_lock(&memphy_lock);
    if (mp->maxsz <= 0 || fpn >= (addr_t)(mp->maxsz / frame_size())) {
        pthread_mutex_unlock(&memphy_lock);
        return -1;
    }
    /* O(F) search also rejects freeing an unallocated/already-free frame. */
    link = &mp->used_fp_list;
    while (*link != NULL && (*link)->fpn != fpn)
        link = &(*link)->fp_next;
    if (*link == NULL) {
        pthread_mutex_unlock(&memphy_lock);
        return -1;
    }
    node = *link;
    *link = node->fp_next;
    node->owner = NULL;
    node->fp_next = mp->free_fp_list;
    mp->free_fp_list = node;
    pthread_mutex_unlock(&memphy_lock);
    return 0;
}

static int valid_byte(struct memphy_struct *mp, addr_t address)
{
    return mp->storage != NULL && mp->maxsz > 0 && address < (addr_t)mp->maxsz;
}

int MEMPHY_mv_csr(struct memphy_struct *mp, addr_t offset)
{
    int result = -1;
    if (mp == NULL)
        return -1;
    pthread_mutex_lock(&memphy_lock);
    if (valid_byte(mp, offset)) {
        mp->cursor = (int)offset;
        result = 0;
    }
    pthread_mutex_unlock(&memphy_lock);
    return result;
}

int MEMPHY_read(struct memphy_struct *mp, addr_t addr, BYTE *value)
{
    if (mp == NULL || value == NULL)
        return -1;
    pthread_mutex_lock(&memphy_lock);
    if (!valid_byte(mp, addr)) {
        pthread_mutex_unlock(&memphy_lock);
        return -1;
    }
    if (!mp->rdmflg)
        mp->cursor = (int)addr;
    *value = mp->storage[addr];
    pthread_mutex_unlock(&memphy_lock);
    return 0;
}

int MEMPHY_write(struct memphy_struct *mp, addr_t addr, BYTE value)
{
    if (mp == NULL)
        return -1;
    pthread_mutex_lock(&memphy_lock);
    if (!valid_byte(mp, addr)) {
        pthread_mutex_unlock(&memphy_lock);
        return -1;
    }
    if (!mp->rdmflg)
        mp->cursor = (int)addr;
    mp->storage[addr] = value;
    pthread_mutex_unlock(&memphy_lock);
    return 0;
}

int MEMPHY_seq_read(struct memphy_struct *mp, addr_t addr, BYTE *value)
{
    return mp != NULL && !mp->rdmflg ? MEMPHY_read(mp, addr, value) : -1;
}

int MEMPHY_seq_write(struct memphy_struct *mp, addr_t addr, BYTE value)
{
    return mp != NULL && !mp->rdmflg ? MEMPHY_write(mp, addr, value) : -1;
}

int MEMPHY_dump(struct memphy_struct *mp)
{
    if (mp == NULL)
        return -1;
    pthread_mutex_lock(&memphy_lock);
    for (int i = 0; i < mp->maxsz; ++i)
        if (mp->storage[i] != 0)
            printf("RAM[0x%x] = 0x%02x\n", (unsigned)i,
                   (unsigned)(unsigned char)mp->storage[i]);
    pthread_mutex_unlock(&memphy_lock);
    return 0;
}

/* Lifecycle operation: stop all users of this device before calling it. */
void MEMPHY_destroy(struct memphy_struct *mp)
{
    if (mp == NULL) return;
    pthread_mutex_lock(&memphy_lock);
    destroy_frame_list(mp->free_fp_list);
    destroy_frame_list(mp->used_fp_list);
    free(mp->storage);
    memset(mp, 0, sizeof(*mp));
    pthread_mutex_unlock(&memphy_lock);
}
