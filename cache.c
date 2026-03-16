#include "cache.h"
// pulls in the struct definition, constants, extern declarations
// and function signatures we defined in cache.h

#include <stdio.h> // printf
#include <stdlib.h> // malloc, free, realloc
#include <string.h> // strlen, strcpy, strcmp
#include <time.h> // time()

// these are the actual variables that cache.c declared with 'extern'
// memory is allocated here - every other file just gets a reference via extern

cache_element *head = NULL; // cache starts empty
int cache_size = 0; // zero bytes used at start
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER; // initializes the mutex statically


// private helper function:
// this function does the actual removal work without acquiring the lock
// it's private to this file (static -> not visible outside cache.c)
// the reason we need this is because add_cache_element() already holds the lock
// when it needs to evict - calling the public version would deadlock
// see add_cache_element() holds the lock and then calls remove_cache_element()
// which again tries to lock - that's a deadlock situation. to fix it we we are having a private function
// remove_cache_element_unsafe() that assumes the lock is already held, and only
// calling the locking version from outside

static void remove_cache_element_unsafe(){
    if(head == NULL) // nothing to remove, cache empty
        return;

    // cache_element *prev = head; // lags one step behind , a pointer
    cache_element *cur = head; // walks the list
    cache_element *lru = head; // tracks the least recently used node
    cache_element *lru_prev = head; // tracks the node before lru (for unlinking)

    // traverse the entire list looking for the node with the oldest timestamp
    // the smallest (oldest) lru_time_track value = least recently used
    while (cur->next != NULL){
        if(cur->next->lru_time_track < lru->lru_time_track){
            lru = cur->next; // new candidate for eviction
            lru_prev = cur; // remember the node before it
        }
        // prev = cur;
        cur = cur->next;
    }
        // unlink lru from the list
        if(lru == head){
            head = head->next; // evicting the head.\, just move head forward
        }
        else{
            lru_prev->next = lru->next; // skip over lru
        }

        // subtract this element's total memory footprint from cache_size
        // must match exactly what was added in add_cache_element
        cache_size -= lru->len + sizeof(cache_element) + strlen(lru->url) + 1;

        free(lru->data);
        free(lru->url);
        free(lru);
}

cache_element *find(char *url){ // find() - lock , it walks the list comparing URLs, if found update its timestamp, unlock, return it
    cache_element *site = NULL;

    pthread_mutex_lock(&lock);

    if(head == NULL){
        printf("[cache] empty\n");
        pthread_mutex_unlock(&lock);
        return NULL; // early return keeps nesting flat
    }

    site = head; // start at the front of the list
    while(site != NULL){ // keep going until we fall off the end
        if(strcmp(site->url, url) == 0){ // found a match
            // cache hit : update timestamp so this element looks "fresh"
            // this is what makes it LRU: every access pushes eviction further away
            site->lru_time_track = time(NULL); // mark it as "just used"
            printf("[cache] hit: %s\n", url); 
            break; // stop walking
        }
        site = site->next; // move to the next node
    }

    if(site == NULL){
        printf("[cache] miss: %s\n", url);
    }
    pthread_mutex_unlock(&lock);
    return site; // NULL on miss , pointer to element on hit
    
}

void remove_cache_element(){
    //acquires the lock and then delegates to the unsafe helper
    // this version is for external callers who don't hold the lock
    pthread_mutex_lock(&lock); // pick up the key - if someone else has it, wait
    remove_cache_element_unsafe();
    pthread_mutex_unlock(&lock); // hand the key back
}

int add_cache_element(char *data, int size, char *url){
    pthread_mutex_lock(&lock);

    int element_size = size + 1 + strlen(url) + 1 + sizeof(cache_element);
    // size + 1 -> data bytes + null terminator
    // strlen(url) + 1 -> url string + null terminator
    // sizeof(cache_element) -> the struct itself
    if(element_size > MAX_ELEMENT_SIZE){
        printf("[cache] element too large to cache (%d bytes)\n", element_size);
        pthread_mutex_unlock(&lock);
        return 0; // 0 = too big, not an error, just not cached
    }

    // evict LRU elements until there's enough room
    // we call the UNSAFE version because we already hold the lock
    while(cache_size + element_size > MAX_SIZE){
        printf("[cache] full, evicting LRU elements\n");
        remove_cache_element_unsafe();
    }
    // allocate and populate the new cache node
    cache_element *element = (cache_element *)malloc(sizeof(cache_element)); // allocates memory for the struct itself
    if(element == NULL){                                                     // the box that holds data, url, len, lru_time_track, next
        pthread_mutex_unlock(&lock);                                         // but data and url inside that box are just pointers
        return -1; // malloc failed                                          // they don't have memory yet, they're pointing at garbage
    }

    element->data = (char *)malloc(size + 1); // allocates the actual response bytes and points element->data at them, size + 1 because we need one extra byte for the null terminator \0
    if(element->data == NULL){ // checks because malloc can fail if the system is out of memory - it returns NULL int hat case
        free(element);
        pthread_mutex_unlock(&lock);
        return -1;
    }
    // strcpy copies bytes until is hits a \0 character and stops. HTTP respponses can contain binary data - images, compressed content - which might have \0 bytes in the middle
    // strcpy would stop early and truncate you data. memcpy copies exactly size bytes,
    // no matter what's in them. Then we manually put \0 at the end ourselves
    memcpy(element->data, data, size); //memcpy instead of strcpy - safer for binary data
    element->data[size] = '\0';

    element->url = (char *)malloc(strlen(url) + 1);
    if(element->url == NULL){
        free(element->data);
        free(element);
        pthread_mutex_unlock(&lock);
        return -1;
    }
    //the URL is normal string so strcpy is fine here - urls never contain \0 in the middle
    strcpy(element->url, url);

    element->len = size;
    element->lru_time_track = time(NULL); // stamp it as "just used"
    element->next = head; // prepend to list - O(1) insertion, new node points to current front
    head = element; // new node becomes the new front
    cache_size += element_size; // keep the running total accurateh

    printf("[cache] added: %s (%d bytes)\n", url, element_size);
    pthread_mutex_unlock(&lock);
    return 1; // success
}
