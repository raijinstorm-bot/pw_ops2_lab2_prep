# Semaphores in C

This note focuses on the semaphore patterns that show up in these lab tasks:

- named semaphores with `sem_open()` for unrelated processes
- initialization gates for shared memory setup
- counting access to a limited resource, like keyboards

## What It Is

A semaphore is a kernel-managed counter.

- `sem_wait()` decrements it, blocking if the value is `0`
- `sem_post()` increments it and wakes one waiter

Two common uses:

- binary semaphore: value `1`, used like a lock or one-time init gate
- counting semaphore: value `N`, used to limit how many processes may enter

In this repo, semaphores are mainly used as:

- an init guard before shared memory initialization in `site_task2` and `site_task3`
- a capacity limiter in `polish_lab_l6/sop-keys.c`

## Main Functions

### `sem_open`

```c
sem_t *sem_open(const char *name, int oflag, ...);
```

Arguments:

- `name`: semaphore name, must start with `/`, for example `"/site_task2_sem"`
- `oflag`: `O_CREAT`, `O_EXCL`, or `0`
- `mode`: permissions, used only with `O_CREAT`, usually `0666` or `0644`
- `value`: initial semaphore value, used only with `O_CREAT`

Returns:

- pointer to semaphore on success
- `SEM_FAILED` on error

### `sem_wait`

```c
int sem_wait(sem_t *sem);
```

Blocks until the semaphore value is positive, then decrements it.

### `sem_post`

```c
int sem_post(sem_t *sem);
```

Increments the semaphore and wakes a waiting process if needed.

### `sem_close`

```c
int sem_close(sem_t *sem);
```

Closes the current process handle.

### `sem_unlink`

```c
int sem_unlink(const char *name);
```

Removes the named semaphore from the system.

### `sem_getvalue`

```c
int sem_getvalue(sem_t *sem, int *sval);
```

Reads the current counter value. Useful for debugging, rarely needed in task logic.

## Example 1: Binary Semaphore as Initialization Gate

Use this when many independent processes can start at the same time, but the shared object must be initialized exactly once.

```c
#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <sys/mman.h>

sem_t *init_sem = sem_open("/my_init_sem", O_CREAT, 0666, 1);
if (init_sem == SEM_FAILED)
    perror("sem_open");

if (sem_wait(init_sem) == -1)
    perror("sem_wait");

// Only one process is here at a time
// Safe place for first-time initialization

if (sem_post(init_sem) == -1)
    perror("sem_post");

sem_close(init_sem);
```

Why initial value `1`?

- first process enters immediately
- all later processes wait
- after init, `sem_post()` lets one of them continue

If the initial value were `0`, even the first process would block.

## Example 2: Counting Semaphore for Limited Capacity

This matches the keyboard task pattern.

```c
sem_t *keyboard_sem = sem_open("/sop-sem-0", O_CREAT, 0644, KEYBOARD_CAP);
if (keyboard_sem == SEM_FAILED)
    perror("sem_open");

sem_wait(keyboard_sem);   // wait until there is space at the keyboard

// critical region: one of at most KEYBOARD_CAP students at this keyboard

sem_post(keyboard_sem);   // free the slot
sem_close(keyboard_sem);
```

## Example 3: Clean Up Semaphores From Previous Runs

This is useful when the task expects a clean start.

```c
if (sem_unlink("/site_task2_sem") == -1 && errno != ENOENT)
    perror("sem_unlink");
```

`ENOENT` means the semaphore does not exist yet, which is fine.

## Example 4: Open Existing Semaphore in Another Process

```c
sem_t *sem = sem_open("/site_task2_sem", 0);
if (sem == SEM_FAILED)
    perror("sem_open");
```

Use this when another process already created the semaphore.

## Common Patterns

### Pattern A: Protect First-Time Shared Memory Initialization

```c
sem_t *init_sem = sem_open("/init_sem", O_CREAT, 0666, 1);
int shm_fd = shm_open("/data", O_CREAT | O_RDWR, 0666);
ftruncate(shm_fd, sizeof(shared_data_t));
shared_data_t *data = mmap(NULL, sizeof(shared_data_t),
                           PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

sem_wait(init_sem);
if (data->initialized == 0)
{
    // initialize mutexes, condvars, counters
    data->initialized = 1;
}
sem_post(init_sem);
sem_close(init_sem);
```

### Pattern B: Limit Number of Processes in a Resource Area

```c
sem_t *sem = sem_open("/room", O_CREAT, 0666, 3);
sem_wait(sem);
// at most 3 processes here
sem_post(sem);
```

## Pitfalls

- Name must start with `/` and should not contain additional slashes.
- `sem_close()` does not delete the semaphore; `sem_unlink()` does.
- If a process dies after `sem_wait()` and before `sem_post()`, the semaphore count stays reduced.
- Named semaphores persist after process exit until unlinked.
- Use semaphores for counts or one-time gates, not for protecting complex shared state with invariants. For that, a mutex is usually better.

## When To Use It

Use a semaphore when:

- you need a simple cross-process gate
- you need to count available slots
- you need unrelated processes to synchronize without sharing a pthread object

Prefer a mutex when:

- you are protecting structured shared memory state
- you need to check and update several fields atomically
