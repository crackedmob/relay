#include "thread.h" 
#include "cache.h"          // find(), add_cache_element()
#include "request.h"        // handle_request(), MAX_BYTES
#include "error.h"          // sendErrorMessage()        
#include "proxy_parse.h"    // ParsedRequest

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>         // close(), shutdown()
#include <sys/socket.h>     // send(), recv()
#include <semaphore.h>      // sem_wait(), sem_post()

// we define the semaphore here - thread.h declared it with extern
// every other file that includes thread.h gets a reference to this one
sem_t semaphore;

void *thread_fn(void *socketNew){
    // sem_wait decrements the semaphore by 1
    // if the value is already 0 (max clients reached), this blocks
    // until another thread calls sem_post and frees up a slot
    sem_wait(&semaphore);
    // cast the void pointer back to int pointer, then dereference
    // this is how pthreads passes arguments - always as void*
    int socket = *((int *)socketNew);
    // allocates a 4kb buffer to hold the incoming HTTP request
    char *buffer = (char *)calloc(MAX_BYTES, sizeof(char));
    // calloc is like malloc but zeroes the memory automatically
    // safer than malloc here because we're going to search for
    // "\r\n\r\n" in it - we don't want garbage triggering a false match
    if(buffer == NULL){
        sem_post(&semaphore);
        return NULL;
    }

    // read the request from the client
    // HTTP requests end with a blank line "\r\n\r\n"
    // we keep reading until we see that sequence
    int bytes_received = recv(socket, buffer, MAX_BYTES, 0);
    while(bytes_received > 0){
        int len = strlen(buffer);
        if(strstr(buffer, "\r\n\r\n") != NULL){
            // found the end of headers - request is complete
            break;
        }
        // not complete yet - read more bytes appending to buffer
        bytes_received = recv(socket, buffer + len, MAX_BYTES - len, 0);
    }
    // make a copy of the raw request to use as the cache key
    // we need this because ParsedRequest_parse() will modify buffer
    char *tempReq = (char *)malloc(strlen(buffer) + 1);
    if(tempReq == NULL){
        free(buffer);
        sem_post(&semaphore);
        return NULL;
    }
    strcpy(tempReq, buffer);

    // - check cache first - 
    cache_element *cached = find(tempReq);

    if(cached != NULL){

        // copy data out of cache before releasing it
        // cached pointer could become invalid if another thread evicts it
        int size = cached->len;
        char *local_copy = (char *)malloc(size + 1);
        if(local_copy != NULL){
            memcpy(local_copy, cached->data, size);
            local_copy[size] = '\0';

            int pos = 0;
            char response[MAX_BYTES];

             while(pos < size){
            // figure out how many bytes to send in this chunk
            // either MAX_BYTES or whatever is left - whichever is smaller
            int chunk = (size - pos) < MAX_BYTES ? (size - pos) : MAX_BYTES;
            memset(response, 0, MAX_BYTES);
            memcpy(response, local_copy + pos, chunk);
            send(socket, response, chunk, 0);
            pos += chunk;
            }
            free(local_copy);
        }
        printf("[thread] cache hit - served from cache\n");
    }
    else if(bytes_received > 0){
        // cache miss - need to fetch from the remote server
        // first parse the raw request into a structured ParsedRequest
        struct ParsedRequest *request = ParsedRequest_create();

        if(ParsedRequest_parse(request, buffer, strlen(buffer)) < 0){
            printf("[thread] failed to parse request\n");
            sendErrorMessage(socket, 400);
        }
        else{
            memset(buffer, 0, MAX_BYTES);

            if(strcmp(request->method, "GET") == 0){
                // valid GET request - check we have everything we need
                if(request->host && request->path && checkHTTPversion(request->version) == 1){
                    int result = handle_request(socket, request, tempReq);
                    if(result == -1){
                        sendErrorMessage(socket, 500);
                    }
                }else{
                    sendErrorMessage(socket, 500);
                }
            }else{
                // else method that isn't GET hits this for now
                // this is where i'll be adding POST?PUT?DELETE support later
                printf("[thread] method not supported: %s\n", request->method);
                sendErrorMessage(socket, 501);
            }
            ParsedRequest_destroy(request);
        }
    }
    else if(bytes_received < 0){
        perror("[thread] error receiving from client\n");
    }
    else{
        printf("[thread] client disconnected\n");
    }
        // shitdown first - tells the OS we're done sending AND receiving
        // then close - releases the file descriptor
        // doing both is cleaner than just close() alone
        shutdown(socket, SHUT_RDWR);
        close(socket);
        free(buffer);
        free(tempReq);
        // release the semaphore slot so another waiting thread can proceed
        sem_post(&semaphore);
        return NULL;
        // pthread function must return void* - we return NULL since
        // we don't need to pass anything back to the caller
}