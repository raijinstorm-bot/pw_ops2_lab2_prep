#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <semaphore.h>

#define KEYBOARD_CAP 10
#define SHARED_MEM_NAME "/memory"
#define MIN_STUDENTS KEYBOARD_CAP
#define MAX_STUDENTS 20
#define MIN_KEYBOARDS 1
#define MAX_KEYBOARDS 5
#define MIN_KEYS 5
#define MAX_KEYS KEYBOARD_CAP

#define ERR(source)                                     \
    do                                                  \
    {                                                   \
        fprintf(stderr, "%s:%d\n", __FILE__, __LINE__); \
        perror(source);                                 \
        kill(0, SIGKILL);                               \
        exit(EXIT_FAILURE);                             \
    } while (0)

void usage(char* program_name)
{
    fprintf(stderr, "Usage: \n");
    fprintf(stderr, "\t%s n m k\n", program_name);
    fprintf(stderr, "\t  n - number of students, %d <= n <= %d\n", MIN_STUDENTS, MAX_STUDENTS);
    fprintf(stderr, "\t  m - number of keyboards, %d <= m <= %d\n", MIN_KEYBOARDS, MAX_KEYBOARDS);
    fprintf(stderr, "\t  k - number of keys in a keyboard, %d <= k <= %d\n", MIN_KEYS, MAX_KEYS);
    exit(EXIT_FAILURE);
}

void ms_sleep(unsigned int milli)
{
    time_t sec = (int)(milli / 1000);
    milli = milli - (sec * 1000);
    struct timespec ts = {0};
    ts.tv_sec = sec;
    ts.tv_nsec = milli * 1000000L;
    if (nanosleep(&ts, &ts))
        ERR("nanosleep");
}

void print_keyboards_state(double* keyboards, int m, int k)
{
    for (int i=0;i<m;++i)
    {
        printf("Klawiatura nr %d:\n", i + 1);
        for (int j=0;j<k;++j)
            printf("  %e", keyboards[i * k + j]);
        printf("\n\n");
    }
}

typedef struct {
    pthread_barrier_t barrier;
    pthread_mutex_t panic_flag_mutex;
    int panic_flag;
    pthread_mutex_t key_mutexes[];
} sync_data_t;


void student_work(const int m, const int k, sync_data_t * sync_data) {
    // BARRIER WAIT: Wait for all n+1 processes (n students + 1 main) to reach this point
    // This ensures no student proceeds until main process has created and initialized the named SHM
    pthread_barrier_wait(&sync_data->barrier);

    // NAMED SEMAPHORES: Open m named semaphores for keyboard access control
    // O_CREAT: create semaphore if it doesn't exist
    // 0644: read/write permissions for owner, read-only for others
    // KEYBOARD_CAP: initial value (max concurrent students per keyboard)
    sem_t **sem_list = (sem_t**)malloc(m * sizeof(sem_t));
    for (int i = 0; i < m; i++) {
        char sem_name[32];
        sprintf(sem_name, "/sop-sem-%d", i);
        sem_list[i] = sem_open(sem_name,  O_CREAT, 0644, KEYBOARD_CAP);
        if (sem_list[i] == SEM_FAILED)
            ERR("sem_open");
    }

    // NAMED SHARED MEMORY: Open the named SHM containing keyboard dirt values
    // SHARED_MEM_NAME: name of the shared memory object (must start with '/')
    // O_RDWR: read and write access (not O_CREAT - main already created it)
    // 0666: read/write permissions (ignored when opening existing SHM)
    int shm_fd = shm_open(SHARED_MEM_NAME, O_RDWR, 0666);
    if (shm_fd == -1)
        ERR("shm_open");

    // MAP NAMED SHM TO MEMORY: Map the named SHM into process address space
    // NULL: let kernel choose mapping address
    // m*k*sizeof(double): size of the mapping (m keyboards, k keys each)
    // PROT_READ|PROT_WRITE: allow reading and writing
    // MAP_SHARED: share changes with other processes (write to underlying SHM)
    // shm_fd: file descriptor from shm_open
    // 0: offset in the SHM (start from beginning)
    double *keyboard_data = (double*)mmap(NULL, m * k * sizeof(double),
                                          PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (keyboard_data == MAP_FAILED)
        ERR("mmap");

    // Close the fd - we no longer need it after mmap
    close(shm_fd);

    srand(getpid());
    while (1) {
        // Check panic flag - if set, run away in panic
        pthread_mutex_lock(&sync_data->panic_flag_mutex);
        int panic = sync_data->panic_flag;
        pthread_mutex_unlock(&sync_data->panic_flag_mutex);

        if (panic) {
            printf("Student %d: running away in panic!\n", getpid());
            break;
        }

        int keybord_ind = rand() % m;  // Choose random keyboard
        int key_idx = rand() % k;      // Choose random key
        int mutex_idx = keybord_ind * k + key_idx;  // Global index for this key's mutex

        sem_t* cur_sem = sem_list[keybord_ind];

        // SEMAPHORE WAIT: Wait if KEYBOARD_CAP students already at this keyboard
        // If semaphore > 0, decrement and proceed
        // If semaphore = 0, block until another student releases it
        sem_wait(cur_sem);

        // MUTEX LOCK: Protect this specific key from concurrent access
        // ROBUST mutex returns EOWNERDEAD if previous owner died
        int lock_ret = pthread_mutex_lock(&sync_data->key_mutexes[mutex_idx]);
        if (lock_ret == EOWNERDEAD) {
            // Previous student died holding this mutex!
            // PTHREAD_MUTEX_ROBUST enables this detection
            printf("Student %d: someone is lying here, help!!!\n", getpid());
            fflush(stdout);

            // Mark mutex as consistent - repairs it for future use
            pthread_mutex_consistent(&sync_data->key_mutexes[mutex_idx]);

            // Set panic flag - all students will see this and run away
            pthread_mutex_lock(&sync_data->panic_flag_mutex);
            sync_data->panic_flag = 1;
            pthread_mutex_unlock(&sync_data->panic_flag_mutex);

            // Check panic flag and exit
            printf("Student %d: running away in panic!\n", getpid());
            pthread_mutex_unlock(&sync_data->key_mutexes[mutex_idx]);
            sem_post(cur_sem);
            break;
        }

        ms_sleep(300);

        // 1% CHANCE OF EXHAUSTION - student dies before updating the key
        if (rand() % 100 == 0) {
            printf("Student %d: I have no more strength!\n", getpid());
            fflush(stdout);  // Ensure message is printed before abort
            // Release semaphore so others can use this keyboard
            sem_post(cur_sem);
            // Do NOT release mutex - it stays locked!
            abort();
        }

        // CLEAN KEY: Divide dirt value by 3
        // Each cleaning reduces dirt by factor of 3
        keyboard_data[mutex_idx] /= 3.0;

        printf("Student %d: cleaning keyboard %d, key %d\n", getpid(), keybord_ind + 1, key_idx + 1);

        // MUTEX UNLOCK: Release the key's mutex, allowing other students to clean it
        pthread_mutex_unlock(&sync_data->key_mutexes[mutex_idx]);

        // SEMAPHORE POST: Release the keyboard, increment semaphore value
        // Allows another waiting student to proceed
        sem_post(cur_sem);
    }

    // Close all semaphore references (doesn't destroy the semaphores, just this process's handle)
    for (int i = 0; i < m; i++) {
          sem_close(sem_list[i]);
    }

    // UNMAP NAMED SHM: Remove the mapping from process address space
    // Doesn't destroy the SHM - it persists until main calls shm_unlink
    if (munmap(keyboard_data, m * k * sizeof(double)))
        ERR("munmap");

    free(sem_list);
    return;
}

int main(int argc, char** argv) { 

    if (argc != 4) 
        usage(argv[0]);

    const int n = atoi(argv[1]);
    const int m = atoi(argv[2]);
    const int k = atoi(argv[3]);

    if (n < KEYBOARD_CAP  || n > 20)
    {
        usage(argv[0]);
    }

    
    if (m < 1  || m > 5)
    {
        usage(argv[0]);
    }


    if (k < 5  || k > MAX_KEYS)
    {
        usage(argv[0]);
    }

    // NAMED SEMAPHORES: Remove semaphores from previous runs if they exist
    // sem_unlink removes the semaphore name from the system
    // ENOENT is expected if semaphore doesn't exist - ignore it
    for (int i = 0; i < m; i++) {
        char sem_name[32];
        sprintf(sem_name, "/sop-sem-%d", i);
        if (sem_unlink(sem_name) == -1 && errno != ENOENT)
            ERR("sem_unlink");
    }

    // ANONYMOUS SHARED MEMORY: Create shared memory for barrier, panic flag and mutexes
    // This memory will be inherited by child processes after fork()
    // sync_size = struct overhead + space for m*k mutexes
    // NULL: let kernel choose address
    // PROT_WRITE|PROT_READ: allow reading and writing
    // MAP_SHARED: share with child processes (changes visible to all)
    // MAP_ANONYMOUS: not backed by a file, just memory
    // -1: ignored for MAP_ANONYMOUS (no file descriptor)
    // 0: offset (ignored for MAP_ANONYMOUS)
    size_t sync_size = sizeof(sync_data_t) + (m * k * sizeof(pthread_mutex_t));
    sync_data_t *sync_data;
    if ((sync_data = (sync_data_t*)mmap(NULL, sync_size, PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED)
        ERR("mmap");

    // BARRIER INIT: Create a process-shared barrier
    // PTHREAD_PROCESS_SHARED: barrier can be used across processes (not just threads)
    pthread_barrierattr_t battr;
    pthread_barrierattr_init(&battr);
    pthread_barrierattr_setpshared(&battr, PTHREAD_PROCESS_SHARED);
    pthread_barrier_init(&sync_data->barrier, &battr, n+1);  // Wait for n students + 1 main
    pthread_barrierattr_destroy(&battr);

    // PANIC FLAG INIT: Initialize mutex protecting panic flag and set flag to 0
    pthread_mutexattr_t pattr;
    pthread_mutexattr_init(&pattr);
    pthread_mutexattr_setpshared(&pattr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&sync_data->panic_flag_mutex, &pattr);
    pthread_mutexattr_destroy(&pattr);
    sync_data->panic_flag = 0;

    // MUTEX INIT: Initialize one mutex for each key (total m*k mutexes)
    // PTHREAD_PROCESS_SHARED: mutex can be used across processes
    // PTHREAD_MUTEX_ROBUST: handle case where process dies while holding mutex
    pthread_mutexattr_t mattr;
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&mattr, PTHREAD_MUTEX_ROBUST);
    for (int i = 0; i < m * k; i++) {
        pthread_mutex_init(&sync_data->key_mutexes[i], &mattr);
    }
    pthread_mutexattr_destroy(&mattr);

    for (int i = 0; i < n; i++)
    {
        switch (fork())
        {
            case 0:
                student_work(m, k, sync_data);
                exit(EXIT_SUCCESS);
            case -1:
                ERR("fork");
        }
    }

    ms_sleep(500);

    // NAMED SHARED MEMORY: Create and open a named shared memory object
    // This SHM will contain the keyboard dirt values (m*k doubles)
    // SHARED_MEM_NAME: unique name for the SHM (must start with '/')
    // O_CREAT: create if doesn't exist
    // O_EXCL: error if already exists (prevents conflicts from previous runs)
    // O_RDWR: read and write access
    // 0666: read/write permissions for owner, group, and others
    int shm_fd;
    if ((shm_fd = shm_open(SHARED_MEM_NAME, O_CREAT | O_EXCL | O_RDWR, 0666)) == -1)
        ERR("shm_open");

    // SET SHM SIZE: Named SHM is created with size 0, must resize it
    // m*k*sizeof(double): enough space for m keyboards with k keys each
    if (ftruncate(shm_fd, m * k * sizeof(double)) == -1)
        ERR("ftruncate");

    // MAP NAMED SHM TO MEMORY: Map the named SHM into process address space
    // NULL: let kernel choose mapping address
    // m*k*sizeof(double): size of the mapping
    // PROT_READ|PROT_WRITE: allow reading and writing
    // MAP_SHARED: share changes with other processes (write to underlying SHM)
    // shm_fd: file descriptor from shm_open
    // 0: offset in the SHM (start from beginning)
    double* keybord_values;
    if ((keybord_values = (double*)mmap(NULL, m * k * sizeof(double), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0)) == MAP_FAILED)
        ERR("mmap");

    // Initialize all keyboard keys to 1.0 (full dirt)
    // This must complete BEFORE the barrier is released
    // Otherwise students might read uninitialized data
    for (int i = 0; i < m * k; i++)
        keybord_values[i] = 1.0;

    // BARRIER WAIT: Wait for all students to reach this point
    // After this, all processes can proceed safely:
    // - Students know the named SHM exists and is initialized
    // - Main knows students are ready to start cleaning
    pthread_barrier_wait(&sync_data->barrier);

    while (wait(NULL) > 0)
        ;

    // NAMED SEMAPHORES: Remove all semaphores from the system
    // This cleans up the semaphore names, freeing system resources
    for (int i = 0; i < m; i++) {
        char sem_name[32];
        sprintf(sem_name, "/sop-sem-%d", i);
        if (sem_unlink(sem_name) == -1 && errno != ENOENT)
            ERR("sem_unlink");
    }

    // Print final state of all keyboards
    print_keyboards_state(keybord_values, m, k);

    // UNMAP NAMED SHM: Remove the named SHM mapping from process address space
    // Doesn't destroy the SHM itself - that's done by shm_unlink
    if (munmap(keybord_values, m * k * sizeof(double)))
        ERR("munmap");

    // SHM UNLINK: Remove the named SHM object from the system
    // This frees the SHM name and marks it for deletion
    // SHM is actually freed when all processes unmap it
    if (shm_unlink(SHARED_MEM_NAME) == -1)
        ERR("shm_unlink");

    // DESTROY MUTEXES: Clean up all mutexes before unmapping anonymous SHM
    for (int i = 0; i < m * k; i++) {
        pthread_mutex_destroy(&sync_data->key_mutexes[i]);
    }

    // Destroy panic flag mutex
    pthread_mutex_destroy(&sync_data->panic_flag_mutex);

    // DESTROY BARRIER: Clean up the barrier before unmapping
    pthread_barrier_destroy(&sync_data->barrier);

    // UNMAP ANONYMOUS SHM: Remove the anonymous SHM mapping from process address space
    // Also frees the memory since MAP_ANONYMOUS is not backed by a file
    if (munmap(sync_data, sync_size))
        ERR("munmap");

    printf("Cleaning finished!\n");
    return EXIT_SUCCESS;
}