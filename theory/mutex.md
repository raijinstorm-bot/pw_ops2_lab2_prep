# Shared and Robust Mutexes in C

This note focuses on the mutex patterns that appear in the lab tasks:

- ordinary mutex use
- process-shared mutexes stored in shared memory
- robust mutexes for crash recovery

## What It Is

A mutex allows only one thread or process to enter a critical section at a time.

In these labs, mutexes protect:

- shared counters
- shared arrays
- shared channel state
- per-key or per-shelf resources

## Main Functions

### `pthread_mutex_init`

```c
int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
```

Initializes a mutex.

### `pthread_mutex_lock`

```c
int pthread_mutex_lock(pthread_mutex_t *mutex);
```

Locks the mutex. Blocks if another owner currently holds it.

### `pthread_mutex_unlock`

```c
int pthread_mutex_unlock(pthread_mutex_t *mutex);
```

Unlocks the mutex.

### `pthread_mutex_destroy`

```c
int pthread_mutex_destroy(pthread_mutex_t *mutex);
```

Destroys the mutex after all users are done.

### `pthread_mutexattr_init`

```c
int pthread_mutexattr_init(pthread_mutexattr_t *attr);
```

Creates an attribute object.

### `pthread_mutexattr_setpshared`

```c
int pthread_mutexattr_setpshared(pthread_mutexattr_t *attr, int pshared);
```

Use `PTHREAD_PROCESS_SHARED` when the mutex is stored in shared memory and used across processes.

### `pthread_mutexattr_setrobust`

```c
int pthread_mutexattr_setrobust(pthread_mutexattr_t *attr, int robust);
```

Use `PTHREAD_MUTEX_ROBUST` when the owner may die while holding the mutex.

### `pthread_mutex_consistent`

```c
int pthread_mutex_consistent(pthread_mutex_t *mutex);
```

Repairs a robust mutex after `pthread_mutex_lock()` returned `EOWNERDEAD`.

## Example 1: Ordinary Mutex

```c
pthread_mutex_t mutex;
pthread_mutex_init(&mutex, NULL);

pthread_mutex_lock(&mutex);
// critical section
pthread_mutex_unlock(&mutex);

pthread_mutex_destroy(&mutex);
```

## Example 2: Process-Shared Mutex in Shared Memory

This is the basic pattern from `site_task2`, `site_task3`, and the lecture board example.

```c
pthread_mutexattr_t mattr;
pthread_mutexattr_init(&mattr);
pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);

pthread_mutex_init(&shared->mtx, &mattr);
pthread_mutexattr_destroy(&mattr);
```

This only works when `shared->mtx` lives in shared memory.

## Example 3: Robust Process-Shared Mutex

This is needed when a process may abort or die while holding the lock.

```c
pthread_mutexattr_t mattr;
pthread_mutexattr_init(&mattr);
pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
pthread_mutexattr_setrobust(&mattr, PTHREAD_MUTEX_ROBUST);

pthread_mutex_init(&shared->mtx, &mattr);
pthread_mutexattr_destroy(&mattr);
```

## Example 4: Handle `EOWNERDEAD`

This is the key robust-mutex recovery pattern.

```c
int ret = pthread_mutex_lock(&shared->mtx);
if (ret == EOWNERDEAD)
{
    // The previous owner died while holding the mutex.
    // Shared state may be inconsistent.

    // Repair your shared state here if needed.

    pthread_mutex_consistent(&shared->mtx);
}
else if (ret != 0)
{
    errno = ret;
    perror("pthread_mutex_lock");
}

// mutex is locked here
pthread_mutex_unlock(&shared->mtx);
```

Important:

- after `EOWNERDEAD`, the mutex is already locked by the caller
- call `pthread_mutex_consistent()` only after fixing the shared state

## Example 5: Protect Shared Counters

```c
pthread_mutex_lock(&shared->process_count_mutex);
shared->process_count++;
shared->total_randomized_points += N;
shared->hit_points += local_hits;
pthread_mutex_unlock(&shared->process_count_mutex);
```

## Example 6: Multiple Mutexes and Lock Ordering

This pattern prevents deadlock in the workshop task.

```c
if (idx1 > idx2)
{
    int tmp = idx1;
    idx1 = idx2;
    idx2 = tmp;
}

pthread_mutex_lock(&mutexes[idx1]);
pthread_mutex_lock(&mutexes[idx2]);

// work on shared state

pthread_mutex_unlock(&mutexes[idx2]);
pthread_mutex_unlock(&mutexes[idx1]);
```

Rule:

- always lock multiple mutexes in one global order
- unlock in reverse order

## Example 7: Mutex Protecting Condition Variable State

From the channel task:

```c
pthread_mutex_lock(&channel->data_mtx);
while (channel->status == CHANNEL_EMPTY)
    pthread_cond_wait(&channel->consumer_cv, &channel->data_mtx);

memcpy(buf, channel->data, channel->length);
channel->status = CHANNEL_EMPTY;
pthread_cond_signal(&channel->producer_cv);
pthread_mutex_unlock(&channel->data_mtx);
```

The mutex protects:

- `status`
- `length`
- `data`

## Pitfalls

- A mutex does not share itself across processes unless you set `PTHREAD_PROCESS_SHARED`.
- A process-shared mutex must live in shared memory.
- If a process dies while holding a non-robust shared mutex, other processes may block forever.
- Do not destroy a mutex while another process may still use it.
- For robust mutexes, always think about how to repair shared state after `EOWNERDEAD`.

## When To Use It

Use a mutex when:

- several fields must be checked or updated together
- shared state must stay consistent
- you need ownership semantics rather than just a counter

Use a robust mutex when:

- processes may crash while holding the lock
- the task explicitly requires recovery from dead owners
