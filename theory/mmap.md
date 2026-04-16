# mmap in C

This note covers the memory-mapping patterns used across the lab:

- mapping a file into memory
- creating anonymous shared memory for related processes
- mapping named shared memory returned by `shm_open`

## What It Is

`mmap()` creates a mapping between virtual memory and:

- a file
- a shared memory object
- anonymous memory

With `MAP_SHARED`, writes become visible to other processes that map the same object.

## Main Functions

### `mmap`

```c
void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
```

Arguments:

- `addr`: preferred address, usually `NULL`
- `len`: mapping size in bytes
- `prot`: permissions such as `PROT_READ`, `PROT_WRITE`
- `flags`: usually `MAP_SHARED`, optionally `MAP_ANONYMOUS`
- `fd`: file descriptor or shared-memory descriptor, or `-1` with `MAP_ANONYMOUS`
- `off`: offset in the mapped object, usually `0`

Returns:

- pointer to mapped memory
- `MAP_FAILED` on error

### `munmap`

```c
int munmap(void *addr, size_t len);
```

Removes the mapping.

### `msync`

```c
int msync(void *addr, size_t len, int flags);
```

Flushes changes to the mapped file.

Common flag:

- `MS_SYNC`: wait until data is synchronized

### `ftruncate`

```c
int ftruncate(int fd, off_t length);
```

Resizes a file or SHM object before mapping it.

## Example 1: Map a File and Read It Like an Array

This is the pattern from the file-counting task.

```c
int fd = open("input.txt", O_RDONLY);
if (fd == -1)
    perror("open");

struct stat st;
fstat(fd, &st);

char *data = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
if (data == MAP_FAILED)
    perror("mmap");

for (off_t i = 0; i < st.st_size; i++)
    putchar(data[i]);

munmap(data, st.st_size);
close(fd);
```

## Example 2: Map a File For Writing

This matches the lecture example with `log.txt`.

```c
int fd = open("./log.txt", O_CREAT | O_RDWR | O_TRUNC, 0666);
ftruncate(fd, n * LOG_LEN);

char *log = mmap(NULL, n * LOG_LEN,
                 PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

memcpy(log + idx * LOG_LEN, line, LOG_LEN);
msync(log, n * LOG_LEN, MS_SYNC);
munmap(log, n * LOG_LEN);
close(fd);
```

## Example 3: Anonymous Shared Memory For `fork()` Children

Use this when one process creates children and all of them should share a region.

```c
int *shared_counter = mmap(NULL, sizeof(int),
                           PROT_READ | PROT_WRITE,
                           MAP_SHARED | MAP_ANONYMOUS,
                           -1, 0);
if (shared_counter == MAP_FAILED)
    perror("mmap");

*shared_counter = 0;
```

This pattern is used for:

- shared result arrays
- arrays of mutexes
- shared flags and counters
- barriers shared by parent and children

## Example 4: Map Named Shared Memory Returned By `shm_open`

```c
int shm_fd = shm_open("/site_task2_shm", O_CREAT | O_RDWR, 0666);
ftruncate(shm_fd, sizeof(shared_data_t));

shared_data_t *shared_data = mmap(NULL, sizeof(shared_data_t),
                                  PROT_READ | PROT_WRITE,
                                  MAP_SHARED, shm_fd, 0);
close(shm_fd);
```

This gives independent processes a common region.

## Common Flag Combinations

### File mapping, shared

```c
mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
```

### Anonymous shared memory between parent and child processes

```c
mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
```

### Read-only file view

```c
mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
```

## Pitfalls

- `mmap()` does not allocate file size for you. Resize the file or SHM first with `ftruncate()`.
- Always check for `MAP_FAILED`, not `NULL`.
- `MAP_PRIVATE` means changes are not shared.
- `MAP_ANONYMOUS` requires `fd == -1`.
- `munmap()` needs the same length that was mapped.
- If you map a file and want durable writes, use `msync()`.

## When To Use It

Use `mmap()` when:

- you want file contents available as memory
- you want parent and child processes to share anonymous memory
- you want to map a named SHM object created with `shm_open()`

In these labs, `mmap()` is the bridge between:

- file descriptors or SHM descriptors
- actual usable pointers in your process
