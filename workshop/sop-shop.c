#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SHOP_FILENAME "./shop"
#define MIN_SHELVES 8
#define MAX_SHELVES 256
#define MIN_WORKERS 1
#define MAX_WORKERS 64

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
    fprintf(stderr, "\t%s n m\n", program_name);
    fprintf(stderr, "\t  n - number of items (shelves), %d <= n <= %d\n", MIN_SHELVES, MAX_SHELVES);
    fprintf(stderr, "\t  m - number of workers, %d <= m <= %d\n", MIN_WORKERS, MAX_WORKERS);
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

void swap(int* x, int* y)
{
    int tmp = *y;
    *y = *x;
    *x = tmp;
}

void shuffle(int* array, int n)
{
    for (int i = n - 1; i > 0; i--)
    {
        int j = rand() % (i + 1);
        swap(&array[i], &array[j]);
    }
}

void print_array(int* array, int n)
{
    for (int i = 0; i < n; ++i)
    {
        printf("%3d ", array[i]);
    }
    printf("\n");
}

void worker_work(int* shop, pthread_mutex_t* mutexes, int* running, const int n) {
    printf("[%d] Worker reports for a night shift\n", getpid());
    srand(getpid());
    while (1) {
        int stop = 0;
        pthread_mutex_lock(&mutexes[n]);
        if (*running == 0)
            stop = 1;
        pthread_mutex_unlock(&mutexes[n]);
        
        if (stop == 1) {
            printf("[%d] Worker received signal from manager to stop\n", getpid());
            return;
        }

        //choose two shelfs randomly
        int indx_1 = rand() % n;
        int indx_2 = rand() % n;
        if (indx_2 == indx_1)
            indx_2 = (indx_2 + 1) % n;

        if (indx_1 > indx_2)
            swap(&indx_1, &indx_2);

        //mutex lock 
        pthread_mutex_lock(&mutexes[indx_1]);
        pthread_mutex_lock(&mutexes[indx_2]);
        //swap if needed 
        if (shop[indx_1] > shop[indx_2]) {
            swap(&shop[indx_1], &shop[indx_2]);
            ms_sleep(100);
        }
            
        // mutex unlock
        pthread_mutex_unlock(&mutexes[indx_2]);
        pthread_mutex_unlock(&mutexes[indx_1]);
    }
    return;
}

void manager_work(int* shop, pthread_mutex_t* mutexes, int* running, const int n) {
    printf("[%d] Manager reports for a night shift\n", getpid());
    while (1) {
        int stop = 0;
        pthread_mutex_lock(&mutexes[n]);
        if (*running == 0)
            stop = 1;
        pthread_mutex_unlock(&mutexes[n]);
        
        if (stop == 1) {
            return;
        }

        //cool down for 0.5 sec
        ms_sleep(500);

        //lock the shop array
        for (int i = 0; i < n; i++)
            pthread_mutex_lock(&mutexes[i]);

        print_array(shop, n);
        if (msync(shop, n * sizeof(int), MS_SYNC))
            ERR("msync");

        //check the oreder 
        int ordered = 1;
        for (int i = 0; i< n; i++) 
            if (shop[i] != i) {
                ordered = 0;
                break;
            }
        
        if (ordered) {
            printf("[%d] The shop shelves are sorted\n", getpid());
            pthread_mutex_lock(&mutexes[n]);
            *running = 0;
            pthread_mutex_unlock(&mutexes[n]);
        }

        //ullock the shop array
        for (int i = 0; i < n; i++)
            pthread_mutex_unlock(&mutexes[i]);
    }
    return;
}

int main(int argc, char** argv) { 
    if (argc != 3) 
        usage(argv[0]);

    const int n = atoi(argv[1]);
    const int m = atoi(argv[2]);

    if (MIN_SHELVES > n || n > MAX_SHELVES)
    {
        fprintf(stderr, "n must be between %d and %d, but got %d\n", MIN_SHELVES, MAX_SHELVES, n);
        usage(argv[0]);
    }

    if (MIN_WORKERS > m || m > MAX_WORKERS)
        usage(argv[0]);


    int shop_fd;
    if ((shop_fd = open(SHOP_FILENAME, O_CREAT | O_RDWR | O_TRUNC,0666)) == -1)
        ERR("open");

    if (ftruncate(shop_fd, n * sizeof(int)))
        ERR("ftruncate");

    int* shop;
    if ((shop = (int*)mmap(NULL, n * sizeof(int), PROT_WRITE | PROT_READ, MAP_SHARED, shop_fd, 0)) == MAP_FAILED)
        ERR("mmap");
    if (close(shop_fd))
        ERR("close");

    int* running;
    if ((running = (int*)mmap(NULL, sizeof(int), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED)
        ERR("mmap");
    *running = 1;

    pthread_mutex_t* mutexes;
    if ((mutexes = (pthread_mutex_t*)mmap(NULL, (n + 1)* sizeof(pthread_mutex_t) , PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED)
        ERR("mmap");
    
    

    for (int i = 0; i < n; i++)
        shop[i] = i; 

    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&mutex_attr, PTHREAD_MUTEX_ROBUST);
    for (int i = 0; i < n+1; i++)
        pthread_mutex_init(&mutexes[i], &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);

    shuffle(shop,n);

    print_array(shop,n);

    //worker process
    for (int i = 0; i < m; i++)
    {
        switch (fork())
        {
            case 0:
                worker_work(shop, mutexes,running, n);
                exit(EXIT_SUCCESS);
            case -1:
                ERR("fork");
        }
    }
    
    //manager process
    switch (fork())
        {
            case 0:
                manager_work(shop, mutexes, running, n);
                exit(EXIT_SUCCESS);
            case -1:
                ERR("fork");
    }

    while (wait(NULL) > 0)
        ;
    
    print_array(shop,n);
    if (msync(shop, n * sizeof(int), MS_SYNC))
        ERR("msync");
    if (munmap(shop, n * sizeof(int)))
        ERR("munmap");
    if (munmap(mutexes, (n+1) * sizeof(pthread_mutex_t)))
        ERR("munmap");
    if (munmap(running, sizeof(int)))
        ERR("munmap");

    return EXIT_SUCCESS;
}
