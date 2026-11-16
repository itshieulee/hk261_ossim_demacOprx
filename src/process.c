#include "common.h"
#include "process.h"
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>

struct process_ref {
    struct krnl_t *kernel;
    struct pcb_t *process;
    unsigned references;
    int exiting;
    struct process_ref *next;
};
static struct process_ref *registry;
static pthread_mutex_t registry_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t registry_changed = PTHREAD_COND_INITIALIZER;

int process_register(struct krnl_t *kernel, struct pcb_t *process)
{
    if (!kernel || !process || process->krnl != kernel) return -EINVAL;
    struct process_ref *entry = calloc(1, sizeof(*entry));
    if (!entry) return -ENOMEM;
    entry->kernel = kernel;
    entry->process = process;
    pthread_mutex_lock(&registry_lock);
    for (struct process_ref *it = registry; it; it = it->next) {
        if (it->kernel == kernel && it->process->pid == process->pid) {
            pthread_mutex_unlock(&registry_lock);
            free(entry);
            return -EEXIST;
        }
    }
    entry->next = registry;
    registry = entry;
    pthread_mutex_unlock(&registry_lock);
    return 0;
}

int process_acquire(struct krnl_t *kernel, uint32_t pid, struct process_ref **out)
{
    if (!kernel || !out) return -EINVAL;
    pthread_mutex_lock(&registry_lock);
    for (struct process_ref *it = registry; it; it = it->next) {
        if (it->kernel == kernel && it->process->pid == pid && !it->exiting) {
            if (it->references == UINT_MAX) {
                pthread_mutex_unlock(&registry_lock);
                return -EOVERFLOW;
            }
            ++it->references;
            *out = it;
            pthread_mutex_unlock(&registry_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&registry_lock);
    return -ESRCH;
}

struct pcb_t *process_context(struct process_ref *ref)
{
    return ref ? ref->process : NULL;
}

int process_release(struct process_ref *ref)
{
    if (!ref) return -EINVAL;
    pthread_mutex_lock(&registry_lock);
    if (!ref->references) {
        pthread_mutex_unlock(&registry_lock);
        return -EINVAL;
    }
    --ref->references;
    pthread_cond_broadcast(&registry_changed);
    pthread_mutex_unlock(&registry_lock);
    return 0;
}

int process_begin_exit(struct krnl_t *kernel, uint32_t pid)
{
    if (!kernel) return -EINVAL;
    pthread_mutex_lock(&registry_lock);
    struct process_ref *entry;
    for (entry = registry; entry; entry = entry->next)
        if (entry->kernel == kernel && entry->process->pid == pid) break;
    if (!entry || entry->exiting) {
        pthread_mutex_unlock(&registry_lock);
        return -ESRCH;
    }
    entry->exiting = 1;
    while (entry->references)
        pthread_cond_wait(&registry_changed, &registry_lock);
    struct process_ref **link = &registry;
    while (*link != entry) link = &(*link)->next;
    *link = entry->next;
    pthread_mutex_unlock(&registry_lock);
    free(entry);
    return 0;
}
