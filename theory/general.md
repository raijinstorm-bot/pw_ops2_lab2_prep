# General C Helpers Used In These Tasks

This file collects non-IPC functions that keep showing up in the lab code.

## Memory Helpers

### `memcpy`

```c
void *memcpy(void *dest, const void *src, size_t n);
```

Copies exactly `n` bytes.

Use it when:

- copying fixed-size memory regions
- copying channel payloads
- copying formatted bytes into mapped memory

Example:

```c
memcpy(channel->data, produced_data, length);
memcpy(log + idx * LOG_LEN, buf, LOG_LEN);
```

Do not use `memcpy` if the source and destination overlap. Use `memmove` instead.

### `memmove`

```c
void *memmove(void *dest, const void *src, size_t n);
```

Like `memcpy`, but safe for overlapping regions.

Example:

```c
memmove(buf, buf + 1, len - 1);
```

### `memset`

```c
void *memset(void *s, int c, size_t n);
```

Fills memory with one byte value.

Examples:

```c
memset(array, 0, sizeof(array));
struct sigaction act = {0};
```

## String Helpers

### `strncpy`

```c
char *strncpy(char *dest, const char *src, size_t n);
```

Copies up to `n` bytes.

Be careful:

- it may not append `'\0'` if the source is too long
- it fills the rest with zeroes if the source is short

Typical safe pattern:

```c
strncpy(name, src, SIZE - 1);
name[SIZE - 1] = '\0';
```

### `snprintf`

```c
int snprintf(char *str, size_t size, const char *format, ...);
```

Preferred for building strings safely.

Examples:

```c
snprintf(buf, sizeof(buf), "%7.5f\n", value);
snprintf(sem_name, sizeof(sem_name), "/sem_%s", path + 1);
```

### `sprintf`

```c
int sprintf(char *str, const char *format, ...);
```

Works, but less safe because it does not know the buffer size.

Use only when the maximum output size is clearly bounded.

## Parsing Helpers

### `atoi`

```c
int atoi(const char *nptr);
```

Simple integer parsing.

Good enough for small student tasks, but it does not report detailed errors.

### `strtof`

```c
float strtof(const char *nptr, char **endptr);
```

Useful for parsing floating-point arguments such as integration bounds.

Example:

```c
float a = strtof(argv[1], NULL);
float b = strtof(argv[2], NULL);
```

For stricter validation, check `endptr` and `errno`.

## File Descriptor Helpers

### `open`

```c
int open(const char *path, int oflag, ...);
```

Opens files.

Common flags in these tasks:

- `O_CREAT`
- `O_RDWR`
- `O_TRUNC`
- `O_RDONLY`

### `close`

```c
int close(int fd);
```

Close file descriptors after `mmap()` or when no longer needed.

Example:

```c
int shm_fd = shm_open(name, O_CREAT | O_RDWR, 0666);
shared_t *data = mmap(..., shm_fd, 0);
close(shm_fd);
```

## Time Helpers

### `sleep`

```c
unsigned int sleep(unsigned int seconds);
```

Simple whole-second delay.

### `usleep`

```c
int usleep(useconds_t usec);
```

Microsecond sleep. Used in some older examples.

### `nanosleep`

```c
int nanosleep(const struct timespec *req, struct timespec *rem);
```

More precise sleep.

Example:

```c
struct timespec t = {1, 0};
nanosleep(&t, &t);
```

## Process Helpers

### `fork`

```c
pid_t fork(void);
```

Creates a child process.

Typical pattern:

```c
switch (fork())
{
    case 0:
        // child
        exit(EXIT_SUCCESS);
    case -1:
        perror("fork");
}
```

### `wait` / `waitpid`

```c
pid_t wait(int *wstatus);
pid_t waitpid(pid_t pid, int *wstatus, int options);
```

Use them in the parent to collect child exit status.

Common loop:

```c
while (wait(NULL) > 0)
    ;
```

## Random Helpers

### `srand`

```c
void srand(unsigned int seed);
```

Seed the pseudo-random generator, usually with `getpid()`.

### `rand`

```c
int rand(void);
```

Used for:

- Monte Carlo points
- random shelf indexes
- random crash simulation

Example:

```c
int idx = rand() % n;
double x = ((double)rand() / RAND_MAX) * (b - a) + a;
```

## Error Reporting Helpers

### `perror`

```c
void perror(const char *s);
```

Prints `s` plus a message based on `errno`.

### `errno`

Global error code set by many system calls.

Typical pattern:

```c
if (sem_unlink(name) == -1 && errno != ENOENT)
    perror("sem_unlink");
```

### `kill`

```c
int kill(pid_t pid, int sig);
```

Used in the lab `ERR(...)` macro to terminate the whole process group on fatal failure.

## Signal Helpers

### `sigaction`

```c
int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
```

Install a signal handler.

Example:

```c
void sigint_handler(int sig)
{
    if (sig == SIGINT)
        should_work = 0;
}
```

### `sigemptyset` and `sigaddset`

Used when building signal masks, for example with `sigwait()` or `pthread_sigmask()`.

## Small Patterns Worth Reusing

### Safe fixed-size name construction

```c
char name[64];
snprintf(name, sizeof(name), "/sem_%d", idx);
```

### Copying a produced message into a channel

```c
if (len > CHANNEL_SIZE)
    return 1;
memcpy(channel->data, src, len);
channel->length = len;
```

### Initialize a mapped numeric array

```c
for (int i = 0; i < n; i++)
    array[i] = 1.0;
```

### Handle interrupted waits with retry macro

Many lab files use:

```c
#define TEMP_FAILURE_RETRY(expression) ...
```

This is useful when a syscall may fail with `EINTR`.

## Rule of Thumb

These general helpers are not IPC by themselves, but they are what make IPC code readable and safe:

- `memcpy` and `memset` for raw memory work
- `snprintf` for safe names
- `open`, `close`, `ftruncate` around files and SHM
- `fork`, `wait`, `kill` around process control
- `errno` and `perror` for debugging failures
