#include "request.h"
#include "cache.h"          // for add_cache_element()
#include "error.h"          // for sendErrorMessage()

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>          // for gethostbyname()
#include <arpa/inet.h>
#include <unistd.h>         // for close()

int checkHTTPversion(char *msg){
    if(strncmp(msg, "HTTP/1.1", 8) == 0 || strncmp(msg, "HTTP/1.0", 8) == 0){
        return 1;
    }
    return -1;
}
int connectRemoteServer(char *host_addr, int port_num){
    // create a brand new TCP socket for this connection
    // AF_INET = IPv4, SOCK_STREAM = TCP
    // this socket is separate from the proxy's main socket
    // it's specifically for talking to the remote server
    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
    if(remoteSocket < 0){
        printf("[request] error creating remote socket\n");
        return -1;
    }
    // gethostbyname() takes a hostname like "neverssl.com"
    // and returns a hostnet struct containing its IP address
    // it handles both domain names AND raw IP strings like "142.250.80.46"
    // returns NULL if the host doesn't exist or can't be resolved
    // look up the IP address for the given hostname
    // e.g. "neverssl.com"-> "13.224.161.90"
    struct hostent *host = gethostbyname(host_addr);
    if(host == NULL){
        printf("[request] host not found: %s\n", host_addr);
        close(remoteSocket); // clean up the socket we just created
        return -1;
    }

    // sockaddr_in is the struct that holds the address we wnat to connect to
    // it needs : address family, port number, and IP address
    struct sockaddr_in server_addr;
    //zero out the entire struct before filling it
    //without this, leftover garbage bytes in memory can cause subtle bugs
    // mostly the unpredictable behaviour due the the garbage bytes
    // so what this does is, it sets every byte to zero in this struct before it fills the fields
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    // sin_family tells the OS this is an IPv4 address
    server_addr.sin_port = htons(port_num);
    // htons() = "host to network short"
    // our CPU stores numbers in little-endian order (least significant byte first)
    // but the network protocol requires big-endian (most significant byte first)
    // htons() does that conversion for the port number
    // eg : port 80 in little-endian is 0x5000, in big-endian is 0x0050
    // without htons() you'd connect to the wrong port


    // copy the received IP address from the hostnet struct into server_addr
    // host->h_addr is a pointer to the raw IP bytes
    // host->h_length is how many bytes the IP address is (4 for IPv4)
    memcpy(&server_addr.sin_addr.s_addr, host->h_addr, host->h_length);

    // actually open the TCP connection to the remote server
    // connect() does the TCP three-way handshake {SYN, SYN-ACK, ACK}
    // it blocks until the connection is established or fails
    // we cast to (struct sockaddr*) because connect() accepts any address family
    if(connect(remoteSocket, (struct sockaddr *)&server_addr, 
                sizeof(server_addr)) < 0){
                    printf("[request] connection failed to %s: %d\n", host_addr, port_num);
                    close(remoteSocket);
                    return -1;
                }

                // return the socket file descriptor
                // the caller uses this to send the request and receive the response
                return remoteSocket;
}


int handle_request(int clientSocket, struct ParsedRequest *request, char *tempReq){
    // allocate a working buffer
    // used first to BUUILD the outgoing request string
    // then reused to RECEIVE the incoming response chunks
    char *buf = (char *)malloc(MAX_BYTES);
    if(buf == NULL)
        return -1;

    // builds the request line manually
    // snprintf is safer than strcpy/strcat - it won't overflow the buffer
    // result looks like: "GET /index.html HTTP/1.1\r\n"
    // format: "GET /path HTTP/1.1\r\n"
    snprintf(buf, MAX_BYTES, "GET %s %s\r\n", request->path, request->version);
    size_t len = strlen(buf);
    // len now points to the end of the request line
    // we will append headers starting from buf + len

    // force Connection: close on the outgoing request
    // this tells the remote server: "close the connection after sending the response"
    // without this, the server uses keep-alive and holds the connection open
    // recv() would then hang waiting for more data that never comes
    // because we'd have no way to know the response is complete
    if (ParsedHeader_set(request, "Connection", "close") < 0)
        printf("[request] warning: could not set Connection header\n");
    
    /// HTTP/1.1 requires a Host header - add it if it's missing
    // some servers will reject the request without it
    if (ParsedHeader_get(request, "Host") == NULL)
        ParsedHeader_set(request, "Host", request->host);

    // unparse_header writes all the headers in to into buf starting at buf + len
    // so buf now contains the full request: request line + all headers
    if(ParsedRequest_unparse_headers(request, buf + len, MAX_BYTES - len) < 0)
        printf("[request] warning: could not unparse headers\n");


    // use port 80 by default (standard HTTP port)
    // if the request specified a port (e.g. http;//example.coom:8080/) use that
    int server_port = 80;
    if(request->port != NULL)
        server_port = atoi(request->port);
    // atoi() converts the port string "8080" to the integer 8080

    // open a TCP connection to the remote server
    int remoteSocketId = connectRemoteServer(request->host, server_port);
    if(remoteSocketId < 0){
        free(buf);
        return -1;
    }

    // send the full request to the remote server
    // strlen(buf) gives us the exact number of bytes to send
    if(send(remoteSocketId, buf, strlen(buf), 0) < 0){
        printf("[request] error sending to remote server\n");
        free(buf);
        close(remoteSocketId);
        return -1;
    }

    // receiving response and forwarding it to the client
    // we receive in chunks because responses can be larger than MAX_BYTES
    // each chunk is immediately forwarded to the client
    // simultaneously we accumulate everything in temp_buffer for caching
    // temp_buffer accumulates the ENTIRE response so we can cache it
    // it starts at MAX_BYTES and grows dynamically as needed
    char *temp_buffer = (char *)malloc(MAX_BYTES);
    int temp_buffer_size = MAX_BYTES;  // current allocated size
    int temp_buffer_index = 0;         // how many bytes we have accumulated so far

    if(temp_buffer == NULL){
        free(buf);
        close(remoteSocketId);
        return -1;
    }
    // clear buf before using it for receiving
    memset(buf, 0, MAX_BYTES);

    // first recv() call - get the first chunk of the respone
    // MAX_BYTES - 1 leaves room for a null terminator if needed
    int bytes_received = recv(remoteSocketId, buf, MAX_BYTES - 1, 0);

    while(bytes_received > 0){
        // forward this chunk to the client immediately
        // the client starts receiving data right away without waiting
        // fr=or the entire response - this is called streaming
        send(clientSocket, buf, bytes_received, 0);

        // - THE BUG FIX - 
        // original code used 'bytes_send' (return of send()) in this loop
        // but send() can send FEWER bytes than received in one call
        // and its return is how many bytes WERE sent, not received
        // we want to accumulate exactly what we received from the server
        // so we use 'bytes_received' here, not 'bytes_send'

        // check if temp_buffer needs to grow before we copy into it
        // if current index + new chunk would overflow, realloc more space
        if(temp_buffer_index + bytes_received >= temp_buffer_size){
            temp_buffer_size += MAX_BYTES;
            // save realloc result to new_buf first - never realloc directly into
            // the original pointer because if realloc fails it returns NULL
            // and we'd lose the pointer to the existing buffer (memory leak)
            char *new_buf = (char *)realloc(temp_buffer, temp_buffer_size); // the original did realloc and immediately used the old pointer, we save the result to new_buf first.
            if(new_buf == NULL){ // check it's not NULL, then reassign. If realloc fails and you kept using the old pointer, that's a memory corruption bug
                
                // realloc failed - we can't cache this response
                // but we can still finish forwarding it to the client
                printf("[request] realloc failed, stopping cache accumulation\n");
                break;
            }
            temp_buffer = new_buf;  // safe to reassign now
        }

        // copy exactly bytes_received bytes from buf into temp_buffer
        // memcpy is used instead of strcpy because HTTP responses can contain
        // binary data (images, compressed content) with null bytes in the middle
        // strcpy would stop at the first null byte and truncate the data
        memcpy(temp_buffer + temp_buffer_index, buf, bytes_received);
        temp_buffer_index += bytes_received;
        // temp_buffer_index now points to the next empty position

        // clear buf and receive the next chunk
        memset(buf, 0, MAX_BYTES);
        bytes_received = recv(remoteSocketId, buf, MAX_BYTES - 1, 0);
        // when the remote server closes the connection (because of Connection: close)
        // recv() returns 0 and the while loop exits cleanly
    }
    // null terminate the accumulated response
    temp_buffer[temp_buffer_index] = '\0';

    // store full response in cache 
    // tempReq (the original raw request string) is the cache key
    // next time a client makes the same request, find() will return this
    add_cache_element(temp_buffer, temp_buffer_index, tempReq);
    printf("[request] response cached for: %s\n", tempReq);

    // clean up everything
    free(buf);
    free(temp_buffer);
    close(remoteSocketId); // the original left the socket open on some failure cases. Every early return now cleans up properly
    return 0;
}
