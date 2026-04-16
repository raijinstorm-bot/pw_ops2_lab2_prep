## Barriers in C (POSIX)

A **barrier** is a synchronization point where **all threads must arrive before any of them can continue**. It's like a meeting point — nobody moves forward until the whole group is present.

---

### How it works

```
Thread 1: ----work----▶ [BARRIER] --------▶ continues
Thread 2: --------work▶ [BARRIER] --------▶ continues
Thread 3: --work-------▶ [BARRIER] --------▶ continues
                              ↑
                    all 3 must arrive here
                    before anyone proceeds
```

---

### Key Functions

| Function | Purpose |
|---|---|
| `pthread_barrier_init()` | Create barrier for N threads |
| `pthread_barrier_wait()` | Block until all N threads arrive |
| `pthread_barrier_destroy()` | Clean up |

---

### 1. Basic Example

```c
#include <stdio.h>
#include <pthread.h>

#define NUM_THREADS 4

pthread_barrier_t barrier;

void *worker(void *arg) {
    int id = *(int *)arg;

    // Phase 1 — parallel work
    printf("Thread %d: doing phase 1 work\n", id);

    // Wait for all threads to finish phase 1
    pthread_barrier_wait(&barrier);

    // Phase 2 — only starts after ALL threads finish phase 1
    printf("Thread %d: doing phase 2 work\n", id);

    return NULL;
}

int main() {
    pthread_t threads[NUM_THREADS];
    int ids[NUM_THREADS];

    // Initialize barrier for NUM_THREADS threads
    pthread_barrier_init(&barrier, NULL, NUM_THREADS);

    for (int i = 0; i < NUM_THREADS; i++) {
        ids[i] = i;
        pthread_create(&threads[i], NULL, worker, &ids[i]);
    }
    for (int i = 0; i < NUM_THREADS; i++)
        pthread_join(threads[i], NULL);

    pthread_barrier_destroy(&barrier);
    return 0;
}
```

**Output** (phase 1 lines always appear before phase 2 lines):
```
Thread 0: doing phase 1 work
Thread 2: doing phase 1 work
Thread 1: doing phase 1 work
Thread 3: doing phase 1 work
Thread 3: doing phase 2 work   ← only after all 4 arrived
Thread 0: doing phase 2 work
...
```

---

### 2. Multiple Barriers (multi-phase computation)

A common real-world pattern — parallel computation in stages:

```c
#include <stdio.h>
#include <pthread.h>

#define NUM_THREADS 3
#define ARRAY_SIZE  9

pthread_barrier_t barrier;
int data[ARRAY_SIZE];

void *worker(void *arg) {
    int id = *(int *)arg;
    int chunk = ARRAY_SIZE / NUM_THREADS;
    int start = id * chunk;
    int end   = start + chunk;

    // --- Phase 1: each thread fills its chunk ---
    for (int i = start; i < end; i++)
        data[i] = id * 10 + i;

    printf("Thread %d: filled data[%d..%d]\n", id, start, end - 1);
    pthread_barrier_wait(&barrier); // sync point 1

    // --- Phase 2: each thread reads the full array ---
    // (safe now — all threads have written their parts)
    if (id == 0) {
        printf("Full array: ");
        for (int i = 0; i < ARRAY_SIZE; i++)
            printf("%d ", data[i]);
        printf("\n");
    }
    pthread_barrier_wait(&barrier); // sync point 2

    // --- Phase 3: process results ---
    printf("Thread %d: processing results\n", id);

    return NULL;
}

int main() {
    pthread_t threads[NUM_THREADS];
    int ids[NUM_THREADS];

    pthread_barrier_init(&barrier, NULL, NUM_THREADS);

    for (int i = 0; i < NUM_THREADS; i++) {
        ids[i] = i;
        pthread_create(&threads[i], NULL, worker, &ids[i]);
    }
    for (int i = 0; i < NUM_THREADS; i++)
        pthread_join(threads[i], NULL);

    pthread_barrier_destroy(&barrier);
    return 0;
}
```

---

### 3. PTHREAD_BARRIER_SERIAL_THREAD

`pthread_barrier_wait()` returns a special value to **exactly one** thread — you can use this to designate one thread to do cleanup/aggregation work between phases:

```c
int ret = pthread_barrier_wait(&barrier);

if (ret == PTHREAD_BARRIER_SERIAL_THREAD) {
    // Only ONE thread (unspecified which) enters here
    printf("I'm the chosen thread — doing aggregation\n");
    // e.g. sum up results from all threads
} else if (ret != 0) {
    perror("barrier_wait");
}
// All threads continue here after
```

This removes the need for a separate `if (id == 0)` check.

---

### Important Notes

**No cross-process support** — `pthread_barrier_t` only works within a single process (threads only). There is no named/POSIX equivalent for processes like there is with semaphores.

**Reusable** — after all threads pass, the barrier automatically resets and can be used again (e.g. in a loop for repeated phases).

**Deadlock risk** — if fewer threads call `pthread_barrier_wait()` than the count specified in `init()`, everyone blocks forever. Make sure the count always matches exactly.

**Compile flag:**
```bash
gcc barrier.c -o barrier -lpthread
```

---

### When to use a barrier vs other primitivses

| Situation | Use |
|---|---|
| All threads must finish phase before next | **Barrier** |
| Protect shared resource from concurrent access | Mutex |
| Signal one thread from another | Semaphore |
| Wait for a condition to become true | Condition variable |



typedef struct {
    pthread_barrier_t barrier;
    int data[100];
} SharedData;

// In the initializer process:
pthread_barrierattr_t battr;
pthread_barrierattr_init(&battr);
pthread_barrierattr_setpshared(&battr, PTHREAD_PROCESS_SHARED);
pthread_barrier_init(&shm->barrier, &battr, NUM_PROCESSES);
pthread_barrierattr_destroy(&battr);

// In every process (including initializer):
pthread_barrier_wait(&shm->barrier); // blocks until all N processes arrive