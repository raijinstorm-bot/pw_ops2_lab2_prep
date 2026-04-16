
#include "channel.h"
#include "macros.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <semaphore.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

channel_t* channel_open(const char* path) {
    // Use sem_name to initialize semaphore accompanied by channel
    char sem_name[PATH_MAX];
    sprintf(sem_name, "/sem_%s", path + 1);

    // NAMED SEMAPHORE: Serialize first-time channel initialization
    // O_CREAT: create semaphore if it doesn't exist yet
    // 0666: read/write permissions for all users
    // 1: binary semaphore used as initialization guard
    sem_t* init_sem;
    if ((init_sem = sem_open(sem_name, O_CREAT, 0666, 1)) == SEM_FAILED)
        return NULL;

    // NAMED SHARED MEMORY: Open channel shared memory object
    // O_CREAT: create if it doesn't exist yet
    // O_RDWR: read and write access
    int shm_fd;
    if ((shm_fd = shm_open(path, O_CREAT | O_RDWR, 0666)) == -1)
    {
        sem_close(init_sem);
        return NULL;
    }

    // SET SHM SIZE: channel object must have enough space for whole structure
    if (ftruncate(shm_fd, sizeof(channel_t)) == -1)
    {
        close(shm_fd);
        sem_close(init_sem);
        return NULL;
    }

    // MAP NAMED SHM TO MEMORY: Map the channel into process address space
    channel_t* channel;
    if ((channel = (channel_t*)mmap(NULL, sizeof(channel_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0)) == MAP_FAILED)
    {
        close(shm_fd);
        sem_close(init_sem);
        return NULL;
    }

    // Close the fd - mapping stays valid after mmap
    close(shm_fd);

    // SEMAPHORE WAIT: Only one process may initialize the channel internals
    if (TEMP_FAILURE_RETRY(sem_wait(init_sem)) == -1)
    {
        munmap(channel, sizeof(channel_t));
        sem_close(init_sem);
        return NULL;
    }

    // FIRST-TIME INIT: Freshly created shared memory is zeroed, so status tells us
    // whether process-shared synchronization primitives still need initialization
    if (channel->status == CHANNEL_UNINITIALIZED)
    {
        pthread_mutexattr_t mattr;
        pthread_condattr_t cattr;

        if (pthread_mutexattr_init(&mattr))
            ERR("pthread_mutexattr_init");
        if (pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED))
            ERR("pthread_mutexattr_setpshared");
        if (pthread_mutex_init(&channel->data_mtx, &mattr))
            ERR("pthread_mutex_init");
        pthread_mutexattr_destroy(&mattr);

        if (pthread_condattr_init(&cattr))
            ERR("pthread_condattr_init");
        if (pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED))
            ERR("pthread_condattr_setpshared");
        if (pthread_cond_init(&channel->producer_cv, &cattr))
            ERR("pthread_cond_init");
        if (pthread_cond_init(&channel->consumer_cv, &cattr))
            ERR("pthread_cond_init");
        pthread_condattr_destroy(&cattr);

        channel->length = 0;
        channel->status = CHANNEL_EMPTY;
    }

    // SEMAPHORE POST: Let other openers proceed
    if (sem_post(init_sem) == -1)
        ERR("sem_post");
    if (sem_close(init_sem) == -1)
        ERR("sem_close");

    return channel;
}

void channel_close(channel_t* channel) {
    if (munmap(channel, sizeof(channel_t)) == -1)
        ERR("munmap");
}

int channel_produce(channel_t* channel, const char* produced_data, uint16_t length) {
    if (length > CHANNEL_SIZE)
        return 1;

    if (pthread_mutex_lock(&channel->data_mtx))
        ERR("pthread_mutex_lock");

    while (channel->status == CHANNEL_OCCUPIED)
    {
        if (pthread_cond_wait(&channel->producer_cv, &channel->data_mtx))
            ERR("pthread_cond_wait");
    }

    memcpy(channel->data, produced_data, length);
    channel->length = length;
    channel->status = CHANNEL_OCCUPIED;

    if (pthread_cond_signal(&channel->consumer_cv))
        ERR("pthread_cond_signal");

    if (pthread_mutex_unlock(&channel->data_mtx))
        ERR("pthread_mutex_unlock");

    return 0;
}

int channel_consume(channel_t* channel, char* consumed_data, uint16_t* length) {
    if (pthread_mutex_lock(&channel->data_mtx))
        ERR("pthread_mutex_lock");

    while (channel->status == CHANNEL_EMPTY)
    {
        if (pthread_cond_wait(&channel->consumer_cv, &channel->data_mtx))
            ERR("pthread_cond_wait");
    }

    if (channel->status == CHANNEL_DEPLETED)
    {
        if (pthread_mutex_unlock(&channel->data_mtx))
            ERR("pthread_mutex_unlock");
        return 1;
    }

    memcpy(consumed_data, channel->data, channel->length);
    *length = channel->length;
    channel->length = 0;
    channel->status = CHANNEL_EMPTY;

    if (pthread_cond_signal(&channel->producer_cv))
        ERR("pthread_cond_signal");

    if (pthread_mutex_unlock(&channel->data_mtx))
        ERR("pthread_mutex_unlock");

    return 0;
}
