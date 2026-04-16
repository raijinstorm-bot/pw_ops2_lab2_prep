#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#define ERR(source)                                     \
    do                                                  \
    {                                                   \
        fprintf(stderr, "%s:%d\n", __FILE__, __LINE__); \
        perror(source);                                 \
        kill(0, SIGKILL);                               \
        exit(EXIT_FAILURE);                             \
    } while (0)

typedef struct {
    pthread_mutex_t mutexes[256];  // One mutex per character
    size_t counts[256];            // Character counts
    int workers_alive;             // Track alive workers
} shared_data_t;

// SAFE LOCK WRAPPER: Handle EOWNERDEAD from robust mutex
// When a worker dies holding a mutex, next worker finds EOWNERDEAD
// pthread_mutex_consistent() repairs the mutex for future use
void safe_lock_mutex(pthread_mutex_t* mutex, int char_idx, shared_data_t* data) {
    int ret = pthread_mutex_lock(mutex);
    if (ret == 0)
        return;
    if (ret == EOWNERDEAD) {
        pthread_mutex_consistent(mutex);
        printf("[%d] Found dead worker at character '%c' (0x%02x)\n",
               getpid(), isprint(char_idx) ? (char)char_idx : '.', char_idx);
        // Lock a dedicated mutex to safely decrement workers_alive
        pthread_mutex_lock(&data->mutexes[255]);
        data->workers_alive--;
        pthread_mutex_unlock(&data->mutexes[255]);
        return;
    }
    errno = ret;
    ERR("pthread_mutex_lock");
}

void child_work(const char* filename, int my_index, int total_workers, shared_data_t* data) {
    // OPEN FILE: Open the file for reading
    // O_RDONLY: read-only access
    int fd = open(filename, O_RDONLY);
    if (fd == -1) ERR("open");

    // GET FILE SIZE: Use fstat to get file statistics including size
    struct stat st;
    if (fstat(fd, &st) == -1) ERR("fstat");
    size_t file_size = st.st_size;

    // MAP FILE TO MEMORY: Map file into process address space
    // NULL: let kernel choose address
    // file_size: size of the mapping
    // PROT_READ: read-only access
    // MAP_PRIVATE: copy-on-write mapping (changes not reflected to file)
    // fd: file descriptor from open()
    // 0: offset in the file (start from beginning)
    char* file_data = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (file_data == MAP_FAILED) ERR("mmap");
    close(fd);

    // DIVIDE WORK: Each child processes a chunk of the file
    size_t chunk_size = file_size / total_workers;
    size_t start = my_index * chunk_size;
    size_t end = (my_index == total_workers - 1) ? file_size : (my_index + 1) * chunk_size;

    // COUNT CHARACTERS: Iterate through this child's chunk
    for (size_t i = start; i < end; i++) {
        unsigned char byte = file_data[i];
        // Lock the mutex for this specific character
        safe_lock_mutex(&data->mutexes[byte], byte, data);
        data->counts[byte]++;
        pthread_mutex_unlock(&data->mutexes[byte]);
    }

    // 3% CHANCE OF SUDDEN DEATH when "reporting results"
    // This simulates a crash after counting but before successful completion
    if (rand() % 100 < 3) {
        printf("[%d] Crashed while reporting!\n", getpid());
        abort();
    }

    // UNMAP FILE: Remove the file mapping from process address space
    if (munmap(file_data, file_size))
        ERR("munmap");

    exit(EXIT_SUCCESS);
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <filename> <num_processes>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char* filename = argv[1];
    int n = atoi(argv[2]);
    if (n < 1 || n > 30) {
        fprintf(stderr, "num_processes must be between 1 and 30\n");
        return EXIT_FAILURE;
    }

    srand(time(NULL));

    // ANONYMOUS SHARED MEMORY: Create shared memory for results
    // This memory will be inherited by child processes after fork()
    // NULL: let kernel choose address
    // sizeof(shared_data_t): size of the mapping
    // PROT_READ|PROT_WRITE: allow reading and writing
    // MAP_SHARED: share with child processes (changes visible to all)
    // MAP_ANONYMOUS: not backed by a file, just memory
    // -1: ignored for MAP_ANONYMOUS (no file descriptor)
    // 0: offset (ignored for MAP_ANONYMOUS)
    shared_data_t* data = mmap(NULL, sizeof(shared_data_t),
                               PROT_READ | PROT_WRITE,
                               MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (data == MAP_FAILED)
        ERR("mmap");

    // INITIALIZE MUTEXES: Create robust, process-shared mutexes
    // PTHREAD_PROCESS_SHARED: mutex can be used across processes
    // PTHREAD_MUTEX_ROBUST: handle case where process dies while holding mutex
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);

    for (int i = 0; i < 256; i++) {
        pthread_mutex_init(&data->mutexes[i], &attr);
    }
    pthread_mutexattr_destroy(&attr);

    // Initialize counts to zero
    memset(data->counts, 0, sizeof(data->counts));
    data->workers_alive = n;

    // FORK CHILDREN: Create N child processes
    for (int i = 0; i < n; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            child_work(filename, i, n, data);
        } else if (pid < 0) {
            ERR("fork");
        }
    }

    // WAIT FOR CHILDREN: Parent waits for all children to finish
    // wait(NULL) returns -1 with ECHILD when no more children exist
    while (wait(NULL) > 0)
        ;

    // CHECK IF ALL WORKERS COMPLETED: If any died, skip summary
    if (data->workers_alive != n) {
        printf("Computation failed - some workers died (%d/%d alive)\n",
               data->workers_alive, n);
    } else {
        printf("Character counts:\n");
        for (int i = 0; i < 256; i++) {
            if (data->counts[i] > 0) {
                if (isprint(i)) {
                    printf("'%c' (%3d): %zu\n", (char)i, i, data->counts[i]);
                } else {
                    printf("    (%3d): %zu\n", i, data->counts[i]);
                }
            }
        }
    }

    // CLEANUP: Destroy all mutexes before unmapping
    for (int i = 0; i < 256; i++) {
        pthread_mutex_destroy(&data->mutexes[i]);
    }

    // UNMAP ANONYMOUS SHM: Remove the mapping from process address space
    if (munmap(data, sizeof(shared_data_t)))
        ERR("munmap");

    return EXIT_SUCCESS;
}
