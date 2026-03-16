#ifndef THREAD_H
#define THREAD_H


#include <semaphore.h>


// the semaphore that limits concurrent client connection
// decared here, defined in thread.c
extern sem_t semaphore;

// the thread fucntion - each client connection runs this
// takes a pointer to the client socket descriptor
// returns NULL when done (pthread requirement)
void *thread_fn(void *socketNew);

#endif