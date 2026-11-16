#ifndef OSSIM_PROCESS_H
#define OSSIM_PROCESS_H
#include <stdint.h>
struct krnl_t;
struct pcb_t;
struct process_ref;
int process_register(struct krnl_t *, struct pcb_t *);
int process_acquire(struct krnl_t *, uint32_t, struct process_ref **);
struct pcb_t *process_context(struct process_ref *);
int process_begin_exit(struct krnl_t *, uint32_t);
int process_release(struct process_ref *);
#endif
