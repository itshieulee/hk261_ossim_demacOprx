#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

int empty(struct queue_t *q)
{
        if (q == NULL)
                return 1;
        return (q->size == 0);
}

void enqueue(struct queue_t *q, struct pcb_t *proc)
{
        /* TODO: put a new process to queue [q] */
        if (q == NULL || proc == NULL || q->size >= MAX_QUEUE_SIZE) return;
        
        q->proc[q->size] = proc;
        q->size++;
}

struct pcb_t *dequeue(struct queue_t *q)
{
        /* TODO: return a pcb whose priority is the highest
         * in the queue [q] and remember to remove it from q
         * */
        if (q == NULL || q->size == 0) return NULL;

        // Trong thuật toán MLQ, mỗi ready_queue chỉ chứa các tiến trình cùng mức ưu tiên.
        // Hàng đợi chạy theo cơ chế Round-Robin, nên tiến trình cần lấy ra luôn là phần tử đầu tiên.
        struct pcb_t *highest_proc = q->proc[0];

        // Dời các phần tử phía sau lên 1 vị trí để lấp chỗ trống
        for (int i = 0; i < q->size - 1; i++) {
                q->proc[i] = q->proc[i + 1];
        }
        q->proc[q->size - 1] = NULL; // Xóa tham chiếu ở vị trí cuối cũ
        q->size--;

        return highest_proc;
}

struct pcb_t *purgequeue(struct queue_t *q, struct pcb_t *proc)
{
        /* TODO: remove a specific item from queue
         * */
        if (q == NULL || q->size == 0 || proc == NULL) return NULL;

        for (int i = 0; i < q->size; i++) {
                // Tìm thấy tiến trình cần loại bỏ
                if (q->proc[i] == proc) {
                        struct pcb_t *removed_proc = q->proc[i];
                        
                        // Dời các phần tử phía sau lên
                        for (int j = i; j < q->size - 1; j++) {
                                q->proc[j] = q->proc[j + 1];
                        }
                        q->proc[q->size - 1] = NULL;
                        q->size--;
                        
                        return removed_proc;
                }
        }
        
        return NULL;
}