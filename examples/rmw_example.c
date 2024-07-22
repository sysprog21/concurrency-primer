#include <stdio.h>
#include <stdatomic.h>
#include <threads.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>

#define PRECISION 100 /* upper bound in BPP sum */
#define CACHE_LINE_SIZE 64
#define N_THREADS 64

struct tpool_future {
    void *result;
    void *arg;
    atomic_flag flag;
};

typedef struct job {
    void *(*func)(void *);
    struct tpool_future *future;
    struct job *next, *prev;
} job_t;

typedef struct idle_job {
    _Atomic(job_t *) prev;
    char padding[CACHE_LINE_SIZE -
                 sizeof(_Atomic(job_t *))]; /* avoid false sharing */
    job_t job;
} idle_job_t;

enum state { idle, running, cancelled };

typedef struct tpool {
    atomic_flag initialezed;
    int size;
    thrd_t *pool;
    atomic_int state;
    thrd_start_t func;
    idle_job_t *head; /* job queue is a SPMC ring buffer */
} tpool_t;

static struct tpool_future *tpool_future_create(void *arg)
{
    struct tpool_future *future = malloc(sizeof(struct tpool_future));
    if (future) {
        future->result = NULL;
        future->arg = arg;
        atomic_flag_clear(&future->flag);
        atomic_flag_test_and_set(&future->flag);
    }
    return future;
}

void tpool_future_wait(struct tpool_future *future)
{
    while (atomic_flag_test_and_set(&future->flag))
        ;
}

void tpool_future_destroy(struct tpool_future *future)
{
    free(future->result);
    free(future);
}

static int worker(void *args)
{
    if (!args)
        return EXIT_FAILURE;
    tpool_t *thrd_pool = (tpool_t *)args;

    while (1) {
        /* worker is laid off */
        if (atomic_load(&thrd_pool->state) == cancelled)
            return EXIT_SUCCESS;
        if (atomic_load(&thrd_pool->state) == running) {
            /* worker takes the job */
            job_t *job = atomic_load(&thrd_pool->head->prev);
            /* worker checks if there is only an idle job in the job queue */
            if (job == &thrd_pool->head->job) {
                /* worker says it is idle */
                atomic_store(&thrd_pool->state, idle);
                thrd_yield();
                continue;
            }
            while (!atomic_compare_exchange_weak(&thrd_pool->head->prev, &job,
                                                 job->prev))
                ;
            job->future->result = (void *)job->func(job->future->arg);
            atomic_flag_clear(&job->future->flag);
            free(job);
        } else {
            /* worker is idle */
            thrd_yield();
        }
    };
    return EXIT_SUCCESS;
}

static bool tpool_init(tpool_t *thrd_pool, size_t size)
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

    idle_job_t *idle_job = malloc(sizeof(idle_job_t));
    if (!idle_job) {
        printf("Failed to allocate idle job.\n");
        return false;
    }

    /* idle_job will always be the first job */
    idle_job->job.next = &idle_job->job;
    idle_job->job.prev = &idle_job->job;
    idle_job->prev = &idle_job->job;
    thrd_pool->func = worker;
    thrd_pool->head = idle_job;
    thrd_pool->state = idle;
    thrd_pool->size = size;

    /* employer hires many workers */
    for (size_t i = 0; i < size; i++)
        thrd_create(thrd_pool->pool + i, worker, thrd_pool);

    return true;
}

static void tpool_destroy(tpool_t *thrd_pool)
{
    if (atomic_exchange(&thrd_pool->state, cancelled))
        printf("Thread pool cancelled with jobs still running.\n");

    for (int i = 0; i < thrd_pool->size; i++)
        thrd_join(thrd_pool->pool[i], NULL);

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

/* Use Bailey–Borwein–Plouffe formula to approximate PI */
static void *bbp(void *arg)
{
    int k = *(int *)arg;
    double sum = (4.0 / (8 * k + 1)) - (2.0 / (8 * k + 4)) -
                 (1.0 / (8 * k + 5)) - (1.0 / (8 * k + 6));
    double *product = malloc(sizeof(double));
    if (!product)
        return NULL;

    *product = 1 / pow(16, k) * sum;
    return (void *)product;
}

struct tpool_future *add_job(tpool_t *thrd_pool, void *(*func)(void *),
                             void *arg)
{
    job_t *job = malloc(sizeof(job_t));
    if (!job)
        return NULL;

    struct tpool_future *future = tpool_future_create(arg);
    if (!future) {
        free(job);
        return NULL;
    }

    job->func = func;
    job->future = future;
    job->next = thrd_pool->head->job.next;
    job->prev = &thrd_pool->head->job;
    thrd_pool->head->job.next->prev = job;
    thrd_pool->head->job.next = job;
    if (thrd_pool->head->prev == &thrd_pool->head->job) {
        thrd_pool->head->prev = job;
        /* the previous job of the idle job is itself */
        thrd_pool->head->job.prev = &thrd_pool->head->job;
    }
    return future;
}

static inline void wait_until(tpool_t *thrd_pool, int state)
{
    while (atomic_load(&thrd_pool->state) != state)
        thrd_yield();
}

int main()
{
    int bbp_args[PRECISION];
    struct tpool_future *futures[PRECISION];
    double bbp_sum = 0;

    tpool_t thrd_pool = { .initialezed = ATOMIC_FLAG_INIT };
    if (!tpool_init(&thrd_pool, N_THREADS)) {
        printf("failed to init.\n");
        return 0;
    }
    /* employer ask workers to work */
    atomic_store(&thrd_pool.state, running);

    /* employer wait ... until workers are idle */
    wait_until(&thrd_pool, idle);

    /* employer add more job to the job queue */
    for (int i = 0; i < PRECISION; i++) {
        bbp_args[i] = i;
        futures[i] = add_job(&thrd_pool, bbp, &bbp_args[i]);
    }

    /* employer ask workers to work */
    atomic_store(&thrd_pool.state, running);

    /* employer wait for the result of job */
    for (int i = 0; i < PRECISION; i++) {
        tpool_future_wait(futures[i]);
        bbp_sum += *(double *)(futures[i]->result);
        tpool_future_destroy(futures[i]);
    }

    /* employer destroys the job queue and lays workers off */
    tpool_destroy(&thrd_pool);
    printf("PI calculated with %d terms: %.15f\n", PRECISION, bbp_sum);
    return 0;
}
