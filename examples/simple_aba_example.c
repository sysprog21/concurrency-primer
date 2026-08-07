#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>

atomic_int v = 42;
/* handshake flags: they pin the interleaving down instead of leaving it to
 * the scheduler, as sleeping would.
 */
atomic_bool a_has_read = false, b_done = false;

int threadA(void *args)
{
    (void)args;

    int va = atomic_load(&v);
    printf("A: v = %d\n", va);
    atomic_store(&a_has_read, true);
    /* wait for B; a real program would be preempted here instead */
    while (!atomic_load(&b_done))
        thrd_yield();
    /* v was changed and changed back, so the comparison succeeds and A never
     * learns that v changed at all.
     */
    if (!atomic_compare_exchange_strong(&v, &va, va + 10)) {
        printf("A: CAS failed, v = %d\n", atomic_load(&v));
        return 1;
    }
    printf("A: v = %d\n", atomic_load(&v));

    return 0;
}

int threadB(void *args)
{
    (void)args;

    while (!atomic_load(&a_has_read))
        thrd_yield();

    atomic_fetch_add(&v, 5);
    printf("B: v = %d\n", atomic_load(&v));
    atomic_fetch_sub(&v, 5);
    printf("B: v = %d\n", atomic_load(&v));
    atomic_store(&b_done, true);

    return 0;
}

int main(void)
{
    thrd_t A, B;
    /* Each thread spins on a flag only the other one sets, so an unchecked
     * thrd_create failure would leave the survivor spinning forever.
     */
    if (thrd_create(&A, threadA, NULL) != thrd_success) {
        printf("failed to create thread A.\n");
        return EXIT_FAILURE;
    }
    if (thrd_create(&B, threadB, NULL) != thrd_success) {
        printf("failed to create thread B.\n");
        atomic_store(&b_done, true); /* release A, then reap it */
        thrd_join(A, NULL);
        return EXIT_FAILURE;
    }

    int result_a = 0;
    thrd_join(A, &result_a);
    thrd_join(B, NULL);
    return result_a ? EXIT_FAILURE : EXIT_SUCCESS;
}
