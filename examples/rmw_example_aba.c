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
    /* Padding alone only sizes the struct; the alignment is what keeps the
     * union off a cache line shared with anything else, and it is also what
     * guarantees the 16-byte alignment a double-width CAS requires. It needs
     * an allocation aligned to match: see tpool_init.
     */
    _Alignas(CACHE_LINE_SIZE) union {
        struct {
            _Atomic(job_t *) prev;
            unsigned long long version;
        };
        _Atomic struct versioned_prev {
            job_t *ptr;
            unsigned long long _version;
        } v_prev;
    };
    char padding[CACHE_LINE_SIZE - sizeof(_Atomic(job_t *)) -
                 sizeof(unsigned long long)]; /* avoid false sharing */
    job_t job;
} idle_job_t;

enum state { idle, running, cancelled };

typedef struct tpool {
    atomic_flag initialized;
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
            struct versioned_prev job = atomic_load(&thrd_pool->head->v_prev);
            /* A failed compare-exchange reloads "job", so the idle job has to
             * be ruled out on every iteration, not just once up front.
             */
            while (job.ptr != &thrd_pool->head->job) {
                /* compare 16 byte at once */
                struct versioned_prev next = { .ptr = job.ptr->prev,
                                               ._version = job._version };
                if (atomic_compare_exchange_weak(&thrd_pool->head->v_prev, &job,
                                                 next))
                    break;
            }
            /* worker checks if there is only an idle job in the job queue */
            if (job.ptr == &thrd_pool->head->job) {
                /* worker says it is idle */
                atomic_store(&thrd_pool->state, idle);
                thrd_yield();
                continue;
            }

            job.ptr->future->result =
                (void *)job.ptr->func(job.ptr->future->arg);
            atomic_flag_clear(&job.ptr->future->flag);
            free(job.ptr);
        } else {
            /* worker is idle */
            thrd_yield();
        }
    }
    return EXIT_SUCCESS;
}

static bool tpool_init(tpool_t *thrd_pool, size_t size)
{
    if (atomic_flag_test_and_set(&thrd_pool->initialized)) {
        printf("This thread pool has already been initialized.\n");
        return false;
    }

    assert(size > 0);
    thrd_pool->pool = malloc(sizeof(thrd_t) * size);
    if (!thrd_pool->pool) {
        printf("Failed to allocate thread identifiers.\n");
        /* release the claim, otherwise the pool can never be initialized */
        atomic_flag_clear(&thrd_pool->initialized);
        return false;
    }

    /* aligned_alloc, not malloc: the double-width CAS on v_prev needs the
     * union 16-byte aligned, and the cache line padding is only worth
     * anything if the allocation starts on a cache line boundary.
     */
    idle_job_t *idle_job =
        aligned_alloc(_Alignof(idle_job_t), sizeof(idle_job_t));
    if (!idle_job) {
        printf("Failed to allocate idle job.\n");
        free(thrd_pool->pool);
        atomic_flag_clear(&thrd_pool->initialized);
        return false;
    }

    /* idle_job will always be the first job */
    idle_job->job.next = &idle_job->job;
    idle_job->job.prev = &idle_job->job;
    idle_job->prev = &idle_job->job;
    idle_job->version = 0ULL;
    thrd_pool->func = worker;
    thrd_pool->head = idle_job;
    thrd_pool->state = idle;
    thrd_pool->size = size;

    /* employer hires many workers */
    for (size_t i = 0; i < size; i++) {
        if (thrd_create(thrd_pool->pool + i, worker, thrd_pool) !=
            thrd_success) {
            printf("Failed to create worker %zu.\n", i);
            /* lay off whoever was already hired before giving up */
            atomic_store(&thrd_pool->state, cancelled);
            while (i--)
                thrd_join(thrd_pool->pool[i], NULL);
            free(idle_job);
            free(thrd_pool->pool);
            /* init undoes itself completely, so there is nothing left for
             * tpool_destroy to reclaim and the caller must not call it.
             * Clear the fields anyway, so a later tpool_init has no stale
             * pointer or count to trip over.
             */
            thrd_pool->pool = NULL;
            thrd_pool->head = NULL;
            thrd_pool->size = 0;
            atomic_flag_clear(&thrd_pool->initialized);
            return false;
        }
    }

    return true;
}

static void tpool_destroy(tpool_t *thrd_pool)
{
    if (atomic_exchange(&thrd_pool->state, cancelled) == running)
        printf("Thread pool cancelled with jobs still running.\n");

    for (int i = 0; i < thrd_pool->size; i++)
        thrd_join(thrd_pool->pool[i], NULL);

    /* Workers are all joined, so the queue is ours alone now. Unclaimed jobs
     * own a future that nobody will ever wait on; free both.
     */
    while (thrd_pool->head->prev != &thrd_pool->head->job) {
        job_t *job = thrd_pool->head->prev->prev;
        tpool_future_destroy(thrd_pool->head->prev->future);
        free(thrd_pool->head->prev);
        thrd_pool->head->prev = job;
    }
    free(thrd_pool->head);
    free(thrd_pool->pool);
    atomic_fetch_and(&thrd_pool->state, 0);
    atomic_flag_clear(&thrd_pool->initialized);
}

/* Use the Bailey–Borwein–Plouffe formula to approximate PI */
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

    /* Workers pop from the back and free as they go, but nothing updates the
     * front link on the way, so once the queue has drained head->job.next
     * still names the last job freed. Drop it before linking, otherwise the
     * writes below land in freed memory.
     */
    struct versioned_prev cur = atomic_load(&thrd_pool->head->v_prev);
    bool was_empty = cur.ptr == &thrd_pool->head->job;
    if (was_empty)
        thrd_pool->head->job.next = &thrd_pool->head->job;

    job->func = func;
    job->future = future;
    job->next = thrd_pool->head->job.next;
    job->prev = &thrd_pool->head->job;
    thrd_pool->head->job.next->prev = job;
    thrd_pool->head->job.next = job;

    if (was_empty) {
        /* Publish the pointer and its version in one 16-byte store. Writing
         * the two halves separately would let a worker observe the new job
         * paired with the old version, which is precisely the window the
         * version number exists to close. This store is unsynchronized: it
         * relies on the employer only adding jobs while the pool is idle,
         * which a worker preempted just after its own state check does not
         * honor. That window is the same one the ABA discussion describes.
         */
        struct versioned_prev next = { .ptr = job,
                                       ._version = cur._version + 1 };
        atomic_store(&thrd_pool->head->v_prev, next);
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

    tpool_t thrd_pool = { .initialized = ATOMIC_FLAG_INIT };
    if (!tpool_init(&thrd_pool, N_THREADS)) {
        printf("failed to init.\n");
        return 0;
    }
    /* employer asks workers to work */
    atomic_store(&thrd_pool.state, running);

    /* employer waits ... until workers are idle */
    wait_until(&thrd_pool, idle);

    /* employer adds more jobs to the job queue */
    for (int i = 0; i < PRECISION; i++) {
        bbp_args[i] = i;
        futures[i] = add_job(&thrd_pool, bbp, &bbp_args[i]);
    }

    /* employer asks workers to work */
    atomic_store(&thrd_pool.state, running);

    /* employer waits for the result of the job */
    for (int i = 0; i < PRECISION; i++) {
        tpool_future_wait(futures[i]);
        bbp_sum += *(double *)(futures[i]->result);
        tpool_future_destroy(futures[i]);
    }

    /* employer destroys the job queue and lays workers off. Wait for the
     * workers to park first: tpool_destroy reports on a pool it cancels
     * while running, and a completed future does not by itself mean the
     * last worker has published idle.
     */
    wait_until(&thrd_pool, idle);
    tpool_destroy(&thrd_pool);
    printf("PI calculated with %d terms: %.15f\n", PRECISION, bbp_sum);
    return 0;
}
