#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

// Optional: use these functions to add debug or error prints to your application
//#define DEBUG_LOG(msg,...)
#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    int mutex_rc;
    pthread_mutex_t *mutex = thread_func_args->mutex;

    DEBUG_LOG("Data in thread: mutex: %p, obtain_wait: %i, obtain_release: %i", thread_func_args->mutex, thread_func_args->wait_to_obtain_ms, thread_func_args->wait_to_release_ms);

    // Sleep
    int sleep_return = usleep(thread_func_args->wait_to_obtain_ms * 1000);
    if (sleep_return != 0) {
        ERROR_LOG("Failed to do initial sleep");
        thread_func_args->thread_complete_success = false;
    } else {
        // Attempt to lock passed in mutex
        mutex_rc = pthread_mutex_lock(mutex);

        if (mutex_rc != 0) {
            ERROR_LOG("Failed to lock mutex");
            thread_func_args->thread_complete_success = false;
        } else {
            sleep_return = usleep(thread_func_args->wait_to_release_ms * 1000);

            if (sleep_return != 0) {
                ERROR_LOG("Failed to do second sleep");
                (void) pthread_mutex_unlock(mutex);
                thread_func_args->thread_complete_success = false;
            } else {
                mutex_rc = pthread_mutex_unlock(mutex);
                if (mutex_rc != 0) {
                    ERROR_LOG("Failed to unlock mutex");
                    thread_func_args->thread_complete_success = false;
                }
                else {
                    thread_func_args->thread_complete_success = true;
                }
            }
        }
    }

    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex, int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
    DEBUG_LOG("Data to start thread mutex: %p, obtain_wait: %i, obtain_release: %i", mutex, wait_to_obtain_ms, wait_to_release_ms);
    struct thread_data* data = malloc(sizeof(struct thread_data));
    data->mutex = mutex;
    data->wait_to_obtain_ms = wait_to_obtain_ms;
    data->wait_to_release_ms = wait_to_release_ms;
    data->thread_complete_success = false;

    DEBUG_LOG("Data to pass to thread: mutex: %p, obtain_wait: %i, obtain_release: %i", data->mutex, data->wait_to_obtain_ms, data->wait_to_release_ms);

    int rc = pthread_create(thread, NULL, threadfunc, (void *)data);
    if (rc != 0) {
        ERROR_LOG("Failed to start thread.");
        return false;
    }

    DEBUG_LOG("Thread Started!");

    return true;
}

