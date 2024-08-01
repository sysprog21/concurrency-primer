#include <stdatomic.h>
#include <stdio.h>
#include <threads.h>
#include <unistd.h>
atomic_int v = 42;

int threadA(void *args)
{
    int va;
    do {
        va = atomic_load(&v);
        printf("A: v = %d\n", va);
        /* Ensure thread B do something before comparing */
        thrd_sleep(&(struct timespec){ .tv_sec = 1 }, NULL);
    } while (atomic_compare_exchange_strong(&v, &va, va + 10));
    printf("A: v = %d\n", atomic_load(&v));

    return 0;
}

int threadB(void *args)
{
    atomic_fetch_add(&v, 5);
    printf("B: v = %d\n", atomic_load(&v));
    atomic_fetch_sub(&v, 5);
    printf("B: v = %d\n", atomic_load(&v));

    return 0;
}

int main()
{
    thrd_t A, B;
    thrd_create(&A, threadA, NULL);
    thrd_create(&B, threadB, NULL);
    /* Ensure all threads complete */
    thrd_sleep(&(struct timespec){ .tv_sec = 2 }, NULL);
    return 0;
}
