# Condition Variables in C

This note covers `pthread_cond_t` with the patterns used in the channel task.

## What It Is

A condition variable lets a thread or process sleep until some shared state changes.

Important rule:

- a condition variable is always used together with a mutex

The condition variable itself does not store the state. The state lives in your shared variables, for example:

- `status == CHANNEL_EMPTY`
- `status == CHANNEL_OCCUPIED`
- `status == CHANNEL_DEPLETED`

## Main Functions

### `pthread_cond_init`

```c
int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr);
```

Arguments:

- `cond`: condition variable to initialize
- `attr`: attributes, or `NULL`

### `pthread_condattr_init`

```c
int pthread_condattr_init(pthread_condattr_t *attr);
```

Creates the attribute object.

### `pthread_condattr_setpshared`

```c
int pthread_condattr_setpshared(pthread_condattr_t *attr, int pshared);
```

Use `PTHREAD_PROCESS_SHARED` when the condition variable is stored in shared memory and used by multiple processes.

### `pthread_cond_wait`

```c
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
```

What it does:

- unlocks `mutex`
- blocks
- when awakened, re-locks `mutex`
- returns with the mutex still locked

### `pthread_cond_signal`

```c
int pthread_cond_signal(pthread_cond_t *cond);
```

Wakes one waiting thread or process.

### `pthread_cond_broadcast`

```c
int pthread_cond_broadcast(pthread_cond_t *cond);
```

Wakes all waiters.

### `pthread_cond_destroy`

```c
int pthread_cond_destroy(pthread_cond_t *cond);
```

Destroys the condition variable after everyone is done with it.

## Basic Wait Pattern

Always use a `while`, not an `if`.

```c
pthread_mutex_lock(&mtx);
while (!ready)
{
    pthread_cond_wait(&cv, &mtx);
}
// ready is true, mutex is locked
pthread_mutex_unlock(&mtx);
```

Why `while`?

- wakeups may be spurious
- another process may wake up first and consume the state change

## Example 1: Process-Shared Condition Variable in Shared Memory

```c
typedef struct
{
    int ready;
    pthread_mutex_t mtx;
    pthread_cond_t cv;
} shared_t;

pthread_mutexattr_t mattr;
pthread_condattr_t cattr;

pthread_mutexattr_init(&mattr);
pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
pthread_mutex_init(&shared->mtx, &mattr);

pthread_condattr_init(&cattr);
pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
pthread_cond_init(&shared->cv, &cattr);
```

This is the pattern used in `site_task3/channel.c`.

## Example 2: Consumer Waits For Data

```c
pthread_mutex_lock(&channel->data_mtx);

while (channel->status == CHANNEL_EMPTY)
{
    pthread_cond_wait(&channel->consumer_cv, &channel->data_mtx);
}

if (channel->status == CHANNEL_DEPLETED)
{
    pthread_mutex_unlock(&channel->data_mtx);
    return 1;
}

memcpy(local_buf, channel->data, channel->length);
channel->status = CHANNEL_EMPTY;
pthread_cond_signal(&channel->producer_cv);
pthread_mutex_unlock(&channel->data_mtx);
```

## Example 3: Producer Waits For Empty Slot

```c
pthread_mutex_lock(&channel->data_mtx);

while (channel->status == CHANNEL_OCCUPIED)
{
    pthread_cond_wait(&channel->producer_cv, &channel->data_mtx);
}

memcpy(channel->data, src, len);
channel->length = len;
channel->status = CHANNEL_OCCUPIED;
pthread_cond_signal(&channel->consumer_cv);
pthread_mutex_unlock(&channel->data_mtx);
```

## Example 4: Wake Everyone On Shutdown

If all waiting consumers should stop, signal all of them.

```c
pthread_mutex_lock(&channel->data_mtx);
channel->status = CHANNEL_DEPLETED;
pthread_cond_broadcast(&channel->consumer_cv);
pthread_mutex_unlock(&channel->data_mtx);
```

Use `broadcast` when:

- many waiters may be blocked
- the shared state changed in a way that all of them should re-check

## Pitfalls

- Never call `pthread_cond_wait()` without holding the matching mutex.
- The condition variable does not replace the mutex.
- Do not assume a wakeup means your condition is true.
- Check the actual shared state after every wakeup.
- `signal` wakes one waiter; `broadcast` wakes all.

## When To Use It

Use a condition variable when:

- processes or threads should sleep until shared state changes
- you have producer-consumer style coordination
- you want blocking without busy-waiting

Do not use it when:

- a plain counter semaphore is enough
- you need communication between unrelated processes but do not want shared pthread objects
