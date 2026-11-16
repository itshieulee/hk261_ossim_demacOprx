#include "common.h"
#include "kernel_memory.h"
#include <errno.h>
#include <pthread.h>

#define PAGING64_PAGESZ 4096
static pthread_mutex_t frame_lock = PTHREAD_MUTEX_INITIALIZER;

int MEMPHY_take_contiguous(struct memphy_struct *mp, addr_t count,
                           struct framephy_struct **out)
{
    if (!mp || !out || !count || mp->maxsz <= 0) return -EINVAL;
    addr_t total = (addr_t)mp->maxsz / PAGING64_PAGESZ;
    if (count > total) return -ENOMEM;
    pthread_mutex_lock(&frame_lock);
    addr_t base = 0;
    int found = 0;
    for (struct framephy_struct *candidate = mp->free_fp_list; candidate;
         candidate = candidate->fp_next) {
        base = candidate->fpn;
        if (base >= total || count > total - base) continue;
        addr_t i;
        for (i = 0; i < count; ++i) {
            struct framephy_struct *it = mp->free_fp_list;
            while (it && it->fpn != base + i) it = it->fp_next;
            if (!it) break;
        }
        if (i == count) { found = 1; break; }
    }
    if (!found) { pthread_mutex_unlock(&frame_lock); return -ENOMEM; }
    struct framephy_struct *head = NULL, **tail = &head;
    for (addr_t i = 0; i < count; ++i) {
        struct framephy_struct **link = &mp->free_fp_list;
        while ((*link)->fpn != base + i) link = &(*link)->fp_next;
        struct framephy_struct *node = *link;
        *link = node->fp_next;
        node->fp_next = NULL;
        *tail = node;
        tail = &node->fp_next;
    }
    *out = head;
    pthread_mutex_unlock(&frame_lock);
    return 0;
}

void MEMPHY_return_frames(struct memphy_struct *mp, struct framephy_struct *frames)
{
    if (!mp || !frames) return;
    pthread_mutex_lock(&frame_lock);
    struct framephy_struct *tail = frames;
    while (tail->fp_next) tail = tail->fp_next;
    tail->fp_next = mp->free_fp_list;
    mp->free_fp_list = frames;
    pthread_mutex_unlock(&frame_lock);
}
