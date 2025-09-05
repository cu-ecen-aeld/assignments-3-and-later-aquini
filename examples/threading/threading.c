#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
	struct thread_data *td = (struct thread_data *)thread_param;

	usleep(td->wait_ms[TO_OBTAIN]);
	pthread_mutex_lock(td->lock);
	td->thread_complete_success = true;
	usleep(td->wait_ms[TO_RELEASE]);
	pthread_mutex_unlock(td->lock);

	return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex, int wait_to_obtain_ms, int wait_to_release_ms)
{
	struct thread_data *td;

	td = calloc(1, sizeof (*td));
	if (!td) {
		ERROR_LOG("cannot allocate memory");
		return false;
	}

	td->thread_complete_success = false;
	td->wait_ms[TO_OBTAIN]  = wait_to_obtain_ms;
	td->wait_ms[TO_RELEASE] = wait_to_release_ms;
	td->lock = mutex;

	if (pthread_create(thread, NULL, threadfunc, td) != 0) {
		ERROR_LOG("cannot create thread");
		return false;
	}

	return true;
}

