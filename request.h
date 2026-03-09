#ifndef REQUEST_H
#define REQUEST_H


#include "proxy_parse.h"
// we need this as handle_request takes a ParsedRequest pointer


#define MAX_BYTES 4096
// used for read/write buffer sizes throughout the request handling

// opens a TCP connection to the remote server
// returns the socket file descriptor on success, -1 on failure
int connectRemoteServer(char *host_addr, int port_num);


// forwards the client's request to the remote server
// streams the response back to the client and caches it
// returns 0 on success, -1 on failure
int handle_request(int clientSocket, struct ParsedRequest *request, char *tempReq);

#endif