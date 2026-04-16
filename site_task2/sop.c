#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define SHM_NAME "/site_task2_shm"
#define SEM_NAME "/site_task2_sem"

#define ERR(source) \
    (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), kill(0, SIGKILL), exit(EXIT_FAILURE))

typedef struct
{
    pthread_mutex_t process_count_mutex;
    int process_count;
    uint64_t total_randomized_points;
    uint64_t hit_points;
    float a;
    float b;
} shared_data_t;

volatile sig_atomic_t should_work = 1;

// Values of this function are in range (0,1]
double func(double x)
{
    usleep(2000);
    return exp(-x * x);
}

/**
 * It counts hit points by Monte Carlo method.
 * Use it to process one batch of computation.
 * @param N Number of points to randomize
 * @param a Lower bound of integration
 * @param b Upper bound of integration
 * @return Number of points which was hit.
 */
int randomize_points(int N, float a, float b)
{
    int result = 0;
    for (int i = 0; i < N; ++i)
    {
        double rand_x = ((double)rand() / RAND_MAX) * (b - a) + a;
        double rand_y = ((double)rand() / RAND_MAX);
        double real_y = func(rand_x);

        if (rand_y <= real_y)
            result++;
    }
    return result;
}

/**
 * This function calculates approximation of integral from counters of hit and total points.
 * @param total_randomized_points Number of total randomized points.
 * @param hit_points Number of hit points.
 * @param a Lower bound of integration
 * @param b Upper bound of integration
 * @return The approximation of integral
 */
double summarize_calculations(uint64_t total_randomized_points, uint64_t hit_points, float a, float b)
{
    return (b - a) * ((double)hit_points / (double)total_randomized_points);
}

/**
 * This function locks mutex and can sometime die (it has 2% chance to die).
 * It cannot die if lock would return an error.
 * It doesn't handle any errors. It's users responsibility.
 * Use it only in STAGE 4.
 *
 * @param mtx Mutex to lock
 * @return Value returned from pthread_mutex_lock.
 */
int random_death_lock(pthread_mutex_t* mtx)
{
    int ret = pthread_mutex_lock(mtx);
    if (ret)
        return ret;

    // 2% chance to die
    if (rand() % 50 == 0)
        abort();
    return ret;
}

void usage(char* argv[])
{
    printf("%s a b N - calculating integral with multiple processes\n", argv[0]);
    printf("a - Start of segment for integral (default: -1)\n");
    printf("b - End of segment for integral (default: 1)\n");
    printf("N - Size of batch to calculate before reporting to shared memory (default: 1000)\n");
}

void sigint_handler(int sig)
{
    if (sig == SIGINT)
        should_work = 0;
}

int main(int argc, char* argv[])
{
    if (argc > 4)
        usage(argv);

    float a = -1.0f;
    float b = 1.0f;
    int N = 1000;

    if (argc > 1)
        a = strtof(argv[1], NULL);
    if (argc > 2)
        b = strtof(argv[2], NULL);
    if (argc > 3)
        N = atoi(argv[3]);

    if (b <= a || N <= 0)
        usage(argv);

    struct sigaction act = {0};
    act.sa_handler = sigint_handler;
    if (sigemptyset(&act.sa_mask))
        ERR("sigemptyset");
    if (sigaction(SIGINT, &act, NULL))
        ERR("sigaction");

    srand(getpid());

    // NAMED SEMAPHORE: Serialize first-time initialization of named SHM
    sem_t* init_sem;
    if ((init_sem = sem_open(SEM_NAME, O_CREAT, 0666, 1)) == SEM_FAILED)
        ERR("sem_open");

    // NAMED SHARED MEMORY: Create or open shared segment for cooperating processes
    int shm_fd;
    if ((shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666)) == -1)
        ERR("shm_open");

    // SET SHM SIZE: shared memory must fit the whole shared structure
    if (ftruncate(shm_fd, sizeof(shared_data_t)) == -1)
        ERR("ftruncate");

    // MAP NAMED SHM TO MEMORY: all processes see the same counters and mutex
    shared_data_t* shared_data;
    if ((shared_data = (shared_data_t*)mmap(NULL, sizeof(shared_data_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0)) == MAP_FAILED)
        ERR("mmap");

    close(shm_fd);

    // SEMAPHORE WAIT: only one process may initialize shared objects
    if (sem_wait(init_sem) == -1)
        ERR("sem_wait");

    if (shared_data->process_count == 0)
    {
        pthread_mutexattr_t mattr;
        if (pthread_mutexattr_init(&mattr))
            ERR("pthread_mutexattr_init");
        if (pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED))
            ERR("pthread_mutexattr_setpshared");
        if (pthread_mutex_init(&shared_data->process_count_mutex, &mattr))
            ERR("pthread_mutex_init");
        pthread_mutexattr_destroy(&mattr);

        shared_data->process_count = 0;
        shared_data->total_randomized_points = 0;
        shared_data->hit_points = 0;
        shared_data->a = a;
        shared_data->b = b;
    }

    if (pthread_mutex_lock(&shared_data->process_count_mutex))
        ERR("pthread_mutex_lock");
    if (shared_data->process_count > 0 && (shared_data->a != a || shared_data->b != b))
    {
        if (pthread_mutex_unlock(&shared_data->process_count_mutex))
            ERR("pthread_mutex_unlock");
        fprintf(stderr, "Incompatible integration bounds in shared memory\n");
        exit(EXIT_FAILURE);
    }
    shared_data->process_count++;
    int collaborators = shared_data->process_count;
    if (pthread_mutex_unlock(&shared_data->process_count_mutex))
        ERR("pthread_mutex_unlock");

    if (sem_post(init_sem) == -1)
        ERR("sem_post");
    if (sem_close(init_sem) == -1)
        ERR("sem_close");

    printf("Collaborating processes: %d\n", collaborators);

    while (should_work)
    {
        int local_hits = randomize_points(N, shared_data->a, shared_data->b);

        if (pthread_mutex_lock(&shared_data->process_count_mutex))
            ERR("pthread_mutex_lock");
        shared_data->total_randomized_points += N;
        shared_data->hit_points += local_hits;
        printf("total=%lu hit=%lu approximation=%f\n",
               shared_data->total_randomized_points,
               shared_data->hit_points,
               summarize_calculations(shared_data->total_randomized_points, shared_data->hit_points, shared_data->a, shared_data->b));
        if (pthread_mutex_unlock(&shared_data->process_count_mutex))
            ERR("pthread_mutex_unlock");
    }

    if (pthread_mutex_lock(&shared_data->process_count_mutex))
        ERR("pthread_mutex_lock");
    shared_data->process_count--;
    int is_last = (shared_data->process_count == 0);
    uint64_t total_randomized_points = shared_data->total_randomized_points;
    uint64_t hit_points = shared_data->hit_points;
    float shared_a = shared_data->a;
    float shared_b = shared_data->b;
    if (pthread_mutex_unlock(&shared_data->process_count_mutex))
        ERR("pthread_mutex_unlock");

    if (is_last)
    {
        if (total_randomized_points > 0)
            printf("Final approximation: %f\n", summarize_calculations(total_randomized_points, hit_points, shared_a, shared_b));
        if (pthread_mutex_destroy(&shared_data->process_count_mutex))
            ERR("pthread_mutex_destroy");
        if (shm_unlink(SHM_NAME) == -1)
            ERR("shm_unlink");
        if (sem_unlink(SEM_NAME) == -1)
            ERR("sem_unlink");
    }

    if (munmap(shared_data, sizeof(shared_data_t)) == -1)
        ERR("munmap");

    return EXIT_SUCCESS;
}
