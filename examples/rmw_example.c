#include <stdio.h>
#include <stdatomic.h>
#include <threads.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>

#define PRECISION 100 /* upper bound in BPP sum */

#define CACHE_LINE_SIZE 64
#define N_JOBS 16
#define N_THREADS 8


typedef struct job {
    void *args;
    struct job *next, *prev;
} job_t;

typedef struct idle_job {
    _Atomic(job_t *) prev;
    char padding[CACHE_LINE_SIZE - sizeof(_Atomic(job_t *))];
    job_t job;
} idle_job_t;

enum state { idle, running, cancelled };

typedef struct thread_pool {
    atomic_flag initialezed;
    int size;
    thrd_t *pool;
    atomic_int state;
    thrd_start_t func;
    // job queue is a SPMC ring buffer
    idle_job_t *head;
} thread_pool_t;

static int worker(void *args)
{
    if (!args)
        return EXIT_FAILURE;
    thread_pool_t *thrd_pool = (thread_pool_t *)args;

    while (1) {
        if (atomic_load(&thrd_pool->state) == cancelled)
            return EXIT_SUCCESS;
        if (atomic_load(&thrd_pool->state) == running) {
            // claim the job
            job_t *job = atomic_load(&thrd_pool->head->prev);
            while (!atomic_compare_exchange_weak(&thrd_pool->head->prev, &job,
                                                   job->prev)) {
            }
            if (job->args == NULL) {
                atomic_store(&thrd_pool->state, idle);
            } else {
                printf("Hello from job %d\n", *(int *)job->args);
                free(job->args);
                free(job); // could cause dangling pointer in other threads
            }
        } else {
            /* To auto run when jobs added, set status to running if job queue is not empty.
             * As long as the producer is protected */
            thrd_yield();
            continue;
        }
    };
}

static bool thread_pool_init(thread_pool_t *thrd_pool, size_t size)
{
    if (atomic_flag_test_and_set(&thrd_pool->initialezed)) {
        printf("This thread pool has already been initialized.\n");
        return false;
    }

    assert(size > 0);
    thrd_pool->pool = malloc(sizeof(thrd_t) * size);
    if (!thrd_pool->pool) {
        printf("Failed to allocate thread identifiers.\n");
        return false;
    }

    // May use memory pool for jobs
    idle_job_t *idle_job = malloc(sizeof(idle_job_t));
    if (!idle_job) {
        printf("Failed to allocate idle job.\n");
        return false;
    }
    // idle_job will always be the first job
    idle_job->job.args = NULL;
    idle_job->job.next = &idle_job->job;
    idle_job->job.prev = &idle_job->job;
    idle_job->prev = &idle_job->job;
    thrd_pool->func = worker;
    thrd_pool->head = idle_job;
    thrd_pool->state = idle;
    thrd_pool->size = size;

    for (size_t i = 0; i < size; i++) {
        thrd_create(thrd_pool->pool + i, worker, thrd_pool);
        //TODO: error handling
    }

    return true;
}

static void thread_pool_destroy(thread_pool_t *thrd_pool)
{
    if (atomic_exchange(&thrd_pool->state, cancelled))
        printf("Thread pool cancelled with jobs still running.\n");
    for (int i = 0; i < thrd_pool->size; i++) {
        thrd_join(thrd_pool->pool[i], NULL);
    }
    while (thrd_pool->head->prev != &thrd_pool->head->job) {
        job_t *job = thrd_pool->head->prev->prev;
        free(thrd_pool->head->prev);
        thrd_pool->head->prev = job;
    }
    free(thrd_pool->head);
    free(thrd_pool->pool);
    atomic_fetch_and(&thrd_pool->state, 0);
    atomic_flag_clear(&thrd_pool->initialezed);
}

__attribute__((nonnull(2))) static bool add_job(thread_pool_t *thrd_pool,
                                                void *args)
{
    // May use memory pool for jobs
    job_t *job = malloc(sizeof(job_t));
    if (!job)
        return false;

    // unprotected producer
    job->args = args;
    job->next = thrd_pool->head->job.next;
    job->prev = &thrd_pool->head->job;
    thrd_pool->head->job.next->prev = job;
    thrd_pool->head->job.next = job;
    if (thrd_pool->head->prev == &thrd_pool->head->job) {
        thrd_pool->head->prev = job;
        // trap worker at idle job
        thrd_pool->head->job.prev = &thrd_pool->head->job;
    }

    return true;
}

static inline void wait_until(thread_pool_t *thrd_pool, int state)
{
    while (atomic_load(&thrd_pool->state) != state) {
        thrd_yield();
    }
}

/* Use Bailey–Borwein–Plouffe formula to approximate PI */
static void *bbp(void *arg)
{
    int k = *(int *) arg;
    double sum = (4.0 / (8 * k + 1)) - (2.0 / (8 * k + 4)) -
                 (1.0 / (8 * k + 5)) - (1.0 / (8 * k + 6));
    double *product = malloc(sizeof(double));
    if (product)
        *product = 1 / pow(16, k) * sum;
    return (void *) product;
}

int main()
{
    thread_pool_t thrd_pool = { .initialezed = ATOMIC_FLAG_INIT };
    if (!thread_pool_init(&thrd_pool, N_THREADS)) {
        printf("failed to init.\n");
        return 0;
    }
    for (int i = 0; i < N_JOBS; i++) {
        int *id = malloc(sizeof(int));
        *id = i;
        add_job(&thrd_pool, id);
    }
    // Due to simplified job queue (not protecting producer), starting the pool manually
    atomic_store(&thrd_pool.state, running);
    wait_until(&thrd_pool, idle);
    for (int i = 0; i < N_JOBS; i++) {
        int *id = malloc(sizeof(int));
        *id = i;
        add_job(&thrd_pool, id);
    }
    atomic_store(&thrd_pool.state, running);
    thread_pool_destroy(&thrd_pool);
    return 0;
}
