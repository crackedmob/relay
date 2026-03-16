// what this file has to do:
// 1. read the port number from command line arguments
// 2. initialize the semaphore and mutex
// 3. create the proxy socket, bind it, start listening
// 4. accept incoming connections ina  loop
// 5. spawn a thread for each connection pointing at thread_fn
// 6. handle graceful shutdown when ctrl+c is pressed


#include "cache.h"          // head, cache_size, lock
#include "thread.h"         // thread_fn, semaphore
#include "request.h"        // MAX_BYTES

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>     // signal(), SIGINT


#define MAX_CLIENTS 400
// the proxy's own listening socket
// global so the signal handler can close it
int proxy_socketId;

// array to hold thread IDs
pthread_t tid[MAX_CLIENTS];

// - graceful shutdown -
// this function runs when the user presses ctrl+c (sigint)
// without this, the proxy just dies instantly leaving sockets open
// and cache memory unfreed
void handle_shutdown(int sig){
    printf("\n[main] shutting down relay...");

    // closing the proxy socket causes accept() in the main loop to 
    // return an error, which breaks out of the while(1) loop
    close(proxy_socketId);

    // destroy the mutex and semaphore - releases their resources
    pthread_mutex_destroy(&lock);
    sem_destroy(&semaphore);

    // free the entire cache linked list
    // walk from head to NULL, freeing each node
    cache_element *curr = head;
    while(curr != NULL){
        cache_element *next = curr->next;
        free(curr->data);
        free(curr->url);
        free(curr);
        curr = next;
    }
    printf("[main] cleanup done, goodbye\n");
    exit(0);
}
int main(int argc, char *argv[]){
    // argument checks
    if(argc != 2){
        printf("urage: ./relay <port>\n");
        exit(1);
    }
    int port_number = atoi(argv[1]);
    if(port_number <= 0 || port_number > 65535){
        printf("[main] invalid port number: %s\n", argv[1]);
        exit(1);
    }
    // signal handler
    // register handle_shutdown to run when ctrl+c is pressed
    // instead of the default behaviour which is instant termination
    signal(SIGINT, handle_shutdown);

    // initialize shared resources
    // semaphore starts at MAX_CLIENTS - each accepted connection decrements it
    // when it hits 0, new connection block until a thread finishes
    sem_init(&semaphore, 0, MAX_CLIENTS);

    // mutex for cache - already statically initialized in cache.c
    // but wee call pthread_mutex_init here for explicitness
    pthread_mutex_init(&lock, NULL);

    printf("[main] starting relay on port %d\n", port_number);

    // create proxy socket
    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);
    if(proxy_socketId < 0){
        perror("[main] failed to create socket");
        exit(1);
    }

    // SO_REUSEADDR lets us bind to a port that's still in TIME_WAIT state
    // without this, restarting the proxy quickly after stopping it
    // gives "address already in use" error for -60 seconds
    int reuse = 1;
    if(setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0){
        perror("[main] setsockopt failed");
    }

    // bind and listen
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    // INADDR_ANY means accept connections on any network interface
    // useful if the machine has multiple IP addresses

    if(bind(proxy_socketId, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0){
        perror("[main] bind failed");
        exit(1);
    }
    if(listen(proxy_socketId, MAX_CLIENTS) < 0){
        perror("[main] listen failed");
        exit(1);
    }

    printf("[main] relay listening on port %d\n", port_number);
    printf("[main] press ctrl+c to stop\n");

    // accept loop
    int i = 0;
    int Connected_socketId[MAX_CLIENTS];

    while(1){
        struct sockaddr_in client_addr;
        memset(&client_addr, 0, sizeof(client_addr));
        socklen_t client_len = sizeof(client_addr);

        // accept() blocks until a client connects
        // returns a new socket descriptor just for that client
        int client_socketId = accept(proxy_socketId, (struct sockaddr *)&client_addr, &client_len);
        if(client_socketId < 0){
            // if we get here after ctrl+c, the socket was closed
            // by handle_shutdown - just break out cleanly
            perror("[main] accept failed");
            break;
        }

        // log the client's IP and port
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        printf("[main] client connected: %s:%d\n", client_ip, ntohs(client_addr.sin_port));

        // store socket descriptor and spawn a thread for this client
        Connected_socketId[i] = client_socketId;
        pthread_create(&tid[i], NULL, thread_fn, (void *)&Connected_socketId[i]);

        // FIX: wrap i around instead of overflowing the array
        // original code let i grow forever past MAX_CLIENTS
        i = (i + 1) % MAX_CLIENTS;
    }

    close(proxy_socketId);
    return 0;
}