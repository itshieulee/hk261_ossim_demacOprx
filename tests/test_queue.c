
#include <stdio.h>
#include "queue.h"

#define CHECK(condition) \
    do { if (!(condition)) return 0; } while (0)

static int test_q001(void) {
    struct queue_t q = {0};

    CHECK(empty(&q) == 1);
    CHECK(q.size == 0);
    return 1;
}

static int test_q002(void) {
    struct queue_t q = {0};
    struct pcb_t p1 = {.pid = 1};

    enqueue(&q, &p1);

    CHECK(q.size == 1);
    CHECK(q.proc[0] == &p1);
    CHECK(empty(&q) == 0);
    return 1;
}

static int test_q003(void) {
    struct queue_t q = {0};
    struct pcb_t p1 = {.pid = 1};
    struct pcb_t p2 = {.pid = 2};
    struct pcb_t p3 = {.pid = 3};

    enqueue(&q, &p1);
    enqueue(&q, &p2);
    enqueue(&q, &p3);

    CHECK(q.size == 3);
    CHECK(q.proc[0] == &p1);
    CHECK(q.proc[1] == &p2);
    CHECK(q.proc[2] == &p3);
    return 1;
}

static int test_q004(void) {
    struct queue_t q = {0};
    struct pcb_t p1 = {.pid = 1};
    struct pcb_t p2 = {.pid = 2};

    enqueue(&q, &p1);
    enqueue(&q, &p2);

    struct pcb_t *result = dequeue(&q);

    CHECK(result == &p1);
    CHECK(q.size == 1);
    CHECK(q.proc[0] == &p2);
    return 1;
}

static int test_q005(void) {
    struct queue_t q = {0};
    struct pcb_t p1 = {.pid = 1};
    struct pcb_t p2 = {.pid = 2};
    struct pcb_t p3 = {.pid = 3};

    enqueue(&q, &p1);
    enqueue(&q, &p2);
    enqueue(&q, &p3);

    struct pcb_t *removed = purgequeue(&q, &p2);

    CHECK(removed == &p2);
    CHECK(q.size == 2);
    CHECK(q.proc[0] == &p1);
    CHECK(q.proc[1] == &p3);
    return 1;
}

static int test_q006(void) {
    struct queue_t q = {0};

    CHECK(dequeue(&q) == NULL);
    CHECK(q.size == 0);
    return 1;
}

static int test_q007(void) {
    struct queue_t q = {0};
    struct pcb_t p[MAX_QUEUE_SIZE + 1] = {0};

    for (int i = 0; i < MAX_QUEUE_SIZE; i++) {
        p[i].pid = i + 1;
        enqueue(&q, &p[i]);
    }

    CHECK(q.size == MAX_QUEUE_SIZE);

    // Attempt to exceed capacity
    enqueue(&q, &p[MAX_QUEUE_SIZE]);

    CHECK(q.size == MAX_QUEUE_SIZE);
    CHECK(q.proc[MAX_QUEUE_SIZE - 1] ==
          &p[MAX_QUEUE_SIZE - 1]);

    return 1;
}

static int test_q008(void) {
    struct queue_t q = {0};
    struct pcb_t p1 = {.pid = 1};
    struct pcb_t missing = {.pid = 99};

    enqueue(&q, &p1);

    CHECK(purgequeue(&q, &missing) == NULL);
    CHECK(q.size == 1);
    CHECK(q.proc[0] == &p1);
    return 1;
}

static int test_q009(void) {
    struct queue_t q = {0};
    struct pcb_t p1 = {.pid = 1};

    enqueue(NULL, &p1);
    enqueue(&q, NULL);

    CHECK(dequeue(NULL) == NULL);
    CHECK(purgequeue(NULL, &p1) == NULL);
    CHECK(empty(NULL) == 1);
    CHECK(q.size == 0);
    return 1;
}

int main(void) {
    struct {
        const char *name;
        int (*run)(void);
    } tests[] = {
        {"Q-001 Empty Queue", test_q001},
        {"Q-002 Enqueue One", test_q002},
        {"Q-003 Enqueue Multiple", test_q003},
        {"Q-004 Dequeue FIFO", test_q004},
        {"Q-005 Purge Queue", test_q005},
        {"Q-006 Dequeue Empty", test_q006},
        {"Q-007 Queue Capacity", test_q007},
        {"Q-008 Purge Missing", test_q008},
        {"Q-009 Invalid Arguments", test_q009}
    };

    int passed = 0;
    int total = sizeof(tests) / sizeof(tests[0]);

    for (int i = 0; i < total; i++) {
        int result = tests[i].run();

        printf("[%s] %s\n",
               result ? "PASS" : "FAIL",
               tests[i].name);

        if (result) passed++;
    }

    printf("\nRESULT: %d/%d tests passed\n",
           passed, total);

    return passed == total ? 0 : 1;
}

