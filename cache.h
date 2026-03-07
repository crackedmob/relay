// the header file is a public contract, it says, "here's
// what the cache module offers to the rest of the program."
// other files will include the #include "cache.h" to use the 
// cache without needing to know how it works internally
#ifndef CACHE_H
// if cache_h has not been defined yet, process everyting below
// it is the start of the header guard
// cache_h is just a convention - it matches the filename
#define CACHE_H
// now we define cache_h so if this file gets included  a second time
// the #ifndef above will be false and the whole file gets skipped
// this prevents the compiler seeing the same struct/function declared twice
#include <time.h>
// needd this for 'time_t' type used in lru_time_track
// time_t is just a number that stores unix timestamps (seconds since 1970)
#include <pthread.h>
// need this for 'pthread_mutex_t'
// the mutex (lock) lives in cache.c but its type is defined in this header

#define MAX_SIZE         200 * (1 << 20)  // 200MB total cache limit, max total memory the cache can use
// 1 << 20 means "shift the number 1 left by 20 bits"
#define MAX_ELEMENT_SIZE  10 * (1 << 10)  // 10KB per element limit
// 1 << 10 shift the number 1 left by 10 bits
// it is the max size of a single response, responses larger than this simply won't be cached
typedef struct cache_element { // a new struct type cache_element
    // typedef means we can write 'cache_element' instead of 'struct cache_element'
    // everywhere elese in the code
    char *data; // a pointer to the actual cached HTTP response bytes
                // this is heap allocated (malloc'd) when an element is added
    int len;    // checkes how many bytes are in 'data'
                // see we need this because the data is binary and may contain
                // null bytes, so we cannot just use strlen()
    char *url;  // the full HTTP request string used as the cache key
                // when a new request comes in , we compare it against
                // this to find a hit
    time_t lru_time_track; // stores the last time this element was accessed (as a unix timestamp)
                           // when the cache is full and we need to evict somethnig,
                           // we find the element with the smllest (oldest) value here and remove it
                           // that's the LRU (least recently used) policy
    struct cache_element *next; // pointer to the next node in the LL
                                // inside a struct definition we must write 'struct cache_element'
                                // not just 'cache_element' the typedef isn't available yet at this point
} cache_element; // this closes the struct and simultaneously creates the 'cache_element' typedef
// extern tells other files "these variables exist somewhere,
// don't create new ones." The actual memory is allocated
// in cache.c, Every other file that includes cache.h
// just gets a reference to the same head pointer
// head points to the first node in the cache LL
// global state — declared here, defined in cache.c
extern cache_element *head;
extern int cache_size; // tracks the total bytes currently stored in the cache
                       // used to decide when to start evicting elements
extern pthread_mutex_t lock; // its the mutex that protects the cache from race condition
                             // if two threads try to add/read cache at the same time without this
                             // memory can get corrupted - the lock forces them to take turns
                             // also defined in cache.c , shared here via extern
// public functions
cache_element *find(char *url); // declaration of the find function - takes a url string, runs a pointer
                                // to the matching cache_element, or NULL if not found
                                // the actual implementation (body) lives in cache.c
int add_cache_element(char *data, int size, char *url); // declaration of add - it takes the response data,
                                                        // its size, and the url key
                                                        // return 1 on success, o if alament too big
                                                        // -1 on failure
void remove_cache_element(); // declaration of remove - takes no arguments
                             // finds and deletes the least recently used elements
                             // from the cache

#endif // closes the #ifndef block from the very top
// everything between #ifndef and #endif is the guarded content
// this is called a header guard
// if two files and both includes cache.h, without this 
// the compiler would see the struct definition twice
// and throw an error. The guard makes sure it's only processed once