#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include <threads.h>
#include <stdlib.h>
#include <stdbool.h>

#define CACHE_LINE_SIZE 64

typedef struct job {
    void *args;
    struct job *next;
    struct job *prev;
} job_t;

enum state { idle, running, cancelled };

typedef struct thread_pool {
    atomic_flag initialezed;
    int size;
    thrd_t *pool;
    atomic_int state;
    thrd_start_t func;
    // job queue is a SPMC ring buffer
    job_t *head;
    char padding1[CACHE_LINE_SIZE - 40];
    atomic_uintptr_t tail; // pointer to head->prev
    char padding2[CACHE_LINE_SIZE - sizeof(atomic_uintptr_t)];
} thread_pool_t;

int worker(void *args)
{
    if (!args)
        return EXIT_FAILURE;
    thread_pool_t *thrd_pool = (thread_pool_t *)args;

    while (1) {
        if (atomic_load(&thrd_pool->state) == cancelled) {
            return EXIT_SUCCESS;
        } else if (atomic_load(&thrd_pool->state) == running) {
            // claim the job
            uintptr_t job = atomic_load(&thrd_pool->tail);
            while (!atomic_compare_exchange_strong(
                &thrd_pool->tail, &job, (uintptr_t)(&(*(job_t **)job)->prev))) {
            }
            if ((*(job_t **)job)->args == NULL) {
                // store happens-before while loop
                atomic_store(&thrd_pool->state, idle);
                while (1) {
                    if (atomic_load(&thrd_pool->state) == running)
                        break;
                    // To auto run when jobs added, check if head and tail are different
                    // as long as producer is protected
                }
            } else {
                printf("Hello from job %d\n", *(int *)(*(job_t **)job)->args);
                free((*(job_t **)job)->args);
                free(*(job_t **)job);
            }
        } else {
            continue;
        }
    };
}

bool thread_pool_init(thread_pool_t *thrd_pool, int size)
{
    atomic_flag_test_and_set(&thrd_pool->initialezed); // It's useless anyway

    // TODO: size should be a positive integer
    // malloc with zero size is non-portable
    thrd_pool->pool = malloc(sizeof(thrd_t) * size);
    if (!thrd_pool->pool) {
        printf("Failed to allocate thread identifiers.\n");
        return false;
    }

    // May use memory pool for jobs
    job_t *idle_job = malloc(sizeof(job_t));
    if (!idle_job) {
        printf("Failed to allocate idle job.\n");
        return false;
    }
    // idle_job will always be the first job
    idle_job->args = NULL;
    idle_job->next = idle_job;
    idle_job->prev = idle_job;
    thrd_pool->func = worker;
    thrd_pool->head = idle_job;
    thrd_pool->tail =
        (atomic_uintptr_t)(&thrd_pool->head->prev); // init is not multihtreaded
    thrd_pool->state = idle;
    thrd_pool->size = size;

    for (int i = 0; i < size; i++) {
        thrd_create(thrd_pool->pool + i, worker, thrd_pool);
        //TODO: error handling
    }

    return true;
}

void thread_pool_destroy(thread_pool_t *thrd_pool)
{
    atomic_store(&thrd_pool->state, cancelled);
    free(thrd_pool->pool);
    free(thrd_pool);

    thrd_pool = NULL;
}

__attribute__((nonnull(2))) bool add_job(thread_pool_t *thrd_pool, void *args)
{
    // May use memory pool for jobs
    job_t *job = malloc(sizeof(job_t));
    if (!job)
        return false;

    // unprotected producer
    job->args = args;
    job->next = thrd_pool->head->next;
    job->prev = thrd_pool->head;
    thrd_pool->head->next->prev = job;
    thrd_pool->head->next = job;

    return true;
}

int main()
{
    thread_pool_t thrd_pool;
    int thread_count = 8;
    int job_count = 16;
    if (!thread_pool_init(&thrd_pool, thread_count)) {
        printf("failed to init.\n");
        return 0;
    }
    for (int i = 0; i < job_count; i++) {
        int *id = malloc(sizeof(int));
        *id = i;
        add_job(&thrd_pool, id);
    }
    // Due to simplified job queue (not protecting producer), starting the pool manually
    atomic_fetch_add(&thrd_pool.state, 1);
    sleep(2);
    atomic_fetch_add(&thrd_pool.state, 1);
    return 0;
}
