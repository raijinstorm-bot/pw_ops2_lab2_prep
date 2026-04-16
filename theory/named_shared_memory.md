# Named Shared Memory in C

This note covers POSIX named shared memory with `shm_open()`.

It is the main IPC tool in:

- `site_task2`
- `site_task3`
- the lecture server/client board example

## What It Is

Named shared memory is a kernel object identified by a name such as `"/site_task2_shm"`.

Any unrelated process that knows the name can:

- open the object
- map it with `mmap()`
- access the same bytes

Think of it as:

- shared memory with a filesystem-like name
- similar in spirit to a named FIFO, but memory-based

## Main Functions

### `shm_open`

```c
int shm_open(const char *name, int oflag, mode_t mode);
```

Arguments:

- `name`: name starting with `/`
- `oflag`: `O_CREAT`, `O_EXCL`, `O_RDWR`, `O_RDONLY`
- `mode`: permissions such as `0666`

Returns:

- file descriptor on success
- `-1` on error

### `shm_unlink`

```c
int shm_unlink(const char *name);
```

Removes the name from the system.

### `ftruncate`

```c
int ftruncate(int fd, off_t length);
```

Named SHM starts with length `0`, so you almost always need this before mapping.

### `mmap`

```c
void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
```

Maps the SHM object into process memory.

## Minimal Workflow

```c
int shm_fd = shm_open("/my_shm", O_CREAT | O_RDWR, 0666);
if (shm_fd == -1)
    perror("shm_open");

if (ftruncate(shm_fd, sizeof(shared_data_t)) == -1)
    perror("ftruncate");

shared_data_t *data = mmap(NULL, sizeof(shared_data_t),
                           PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
if (data == MAP_FAILED)
    perror("mmap");

close(shm_fd);
```

## Example 1: Independent Processes Sharing Counters

This is the `site_task2` pattern.

```c
typedef struct
{
    pthread_mutex_t mtx;
    int process_count;
    uint64_t total_randomized_points;
    uint64_t hit_points;
    float a;
    float b;
} shared_data_t;

int shm_fd = shm_open("/site_task2_shm", O_CREAT | O_RDWR, 0666);
ftruncate(shm_fd, sizeof(shared_data_t));

shared_data_t *shared = mmap(NULL, sizeof(shared_data_t),
                             PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
close(shm_fd);
```

## Example 2: Channel Structure in Shared Memory

This is the `site_task3` pattern.

```c
int shm_fd = shm_open(path, O_CREAT | O_RDWR, 0666);
ftruncate(shm_fd, sizeof(channel_t));

channel_t *channel = mmap(NULL, sizeof(channel_t),
                          PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
close(shm_fd);
```

The shared structure can contain:

- plain data fields
- mutexes
- condition variables
- counters
- flags

## Example 3: Existing Process Opens SHM Created By Another Process

This is the lecture board-client pattern.

```c
int shm_fd = shm_open("/12345-board", O_RDWR, 0666);
if (shm_fd == -1)
    perror("shm_open");

char *ptr = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
close(shm_fd);
```

## Initialization Race Problem

`shm_open()` alone is not enough to protect first-time initialization.

Bad scenario:

1. process A opens and maps SHM
2. process B opens and maps SHM
3. both think they are first
4. both initialize the same mutexes and counters

That is why the lab tasks use:

- named SHM for the data
- named semaphore for one-time initialization

## Example 4: SHM + Semaphore Initialization Gate

```c
sem_t *init_sem = sem_open("/my_init_sem", O_CREAT, 0666, 1);
int shm_fd = shm_open("/my_shm", O_CREAT | O_RDWR, 0666);
ftruncate(shm_fd, sizeof(shared_t));

shared_t *shared = mmap(NULL, sizeof(shared_t),
                        PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
close(shm_fd);

sem_wait(init_sem);
if (shared->initialized == 0)
{
    // initialize mutexes, condvars, counters
    shared->initialized = 1;
}
sem_post(init_sem);
sem_close(init_sem);
```

Freshly created POSIX shared memory is zero-filled, so checking a field like `initialized` or `status` is common.

## Cleanup Pattern

Current process:

```c
munmap(shared, sizeof(shared_t));
```

Last process:

```c
shm_unlink("/my_shm");
```

Typical rule:

- every process `munmap()`s its mapping
- only the last logical owner should `shm_unlink()`

## Pitfalls

- Name must start with `/`.
- New SHM object size is `0` until `ftruncate()`.
- `shm_unlink()` removes the name, not necessarily the bytes immediately. Actual deletion happens after last reference is gone.
- Shared memory does not synchronize access. You still need mutexes, semaphores, or condition variables.
- Do not reinitialize shared pthread objects after other processes may already use them.

## When To Use It

Use named shared memory when:

- unrelated processes must share structured data
- you need more than a stream of bytes
- multiple fields, locks, and state flags belong together

Prefer anonymous `mmap(... MAP_SHARED | MAP_ANONYMOUS ...)` when:

- only one parent and its `fork()` children need the shared region
