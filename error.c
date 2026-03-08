// this file basically consists of a single function, which takes a socket and status code and send back 
// a formatted HTTP error response
// when client send a request, and someting goes wrong, we need to send back a proper HTTP response with the right status 
// code so the client knows what happened

#include "error.h"

#include <stdio.h> // printf, snprintf
#include <string.h> // strlen
#include <sys/socket.h> // send()
#include <time.h> // time(), gmtime(), strftime()


int sendErrorMessage(int socket, int status_code){
    char str[1024];
    char currentTime[50];
    time_t now = time(0);

    struct tm data = *gmtime(&now);
    strftime(currentTime, sizeof(currentTime), "%a, %d %b %Y %H: %M:%S %Z", &data);

    switch (status_code){
        case 400:
            snprintf(str, sizeof(str),
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: text/html\r\nDate: %s\r\n\r\n"
            "<HTML><BODY><H1>400 Bad Request</H1></BODY></HTML>",
            currentTime);
        printf("[error] 400 Bad Request\n");
        break;

        case 403:
            snprintf(str, sizeof(str),
            "HTTP/1.1 403 Forbidden\r\n"
            "Content-Type: text/html\r\nDate: %s\r\n\r\n"
            "<HTML><BODY><H1>403 Forbidden</H1></BODY></HTML>",
            currentTime);
        printf("[error] 403 Forbidden\n");
        break;

        case 404:
            snprintf(str, sizeof(str),
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/html\r\nDate: %s\r\n\r\n"
            "<HTML><BODY><H1>404 Not Found</H1></BODY></HTML>",
            currentTime);
        printf("[error] 404 Not Found");

        case 500:
            snprintf(str, sizeof(str),
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: text/html\r\nDate: %s\r\n\r\n"
            "<HTML><BODY><H1>500 Internal Server Error</H1></BODY></HTML>",
            currentTime);
        printf("[error] 500 Internal Server Error\n");
        break;

        case 501:
            snprintf(str, sizeof(str),
            "HTTP/1.1 501 Not Implemented\r\n"
            "Content-Type: text/html\r\nDate: %s\r\n\r\n"
            "<HTML><BODY><H1>501 Not Implemented/H1></BODY></HTML>",
            currentTime);
        printf("[error] 501 Not Implemented\n");
        break;

        case 505:
            snprintf(str, sizeof(str),
            "HTTP/1.1 505 HTTP Version Not Supported\r\n"
            "Content-Type: text/html\r\nDate: %s\r\n\r\n"
            "<HTML><BODY><H1>505 HTTP Version Not Supportedt</H1></BODY></HTML>",
            currentTime);
        printf("[error] 505 HTTP Version Not Supported\n");
        break;

    default:
        return -1; // unrecognized status code
    }

    send(socket, str, strlen(str), 0);
    // send the formatted error string to the client over the socket
    // the client (browser/curl) reads this and displays the error
    return 1;
}
// %s this is where the current time gets inserted in the format string