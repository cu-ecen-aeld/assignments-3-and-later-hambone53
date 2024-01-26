#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Defines
#define SERVER_PORT     "9000"
#define BACK_LOG        10
#define TEMP_FILE       "/var/tmp/aesdsocketdata"
#define MAX_BUF_SIZE    4096

// Types

// File Private Vars

// File private function prototypes


/********************************************************************
*********************************************************************/
// Get Sockaddr, IPv4 or IPv6
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family ==  AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}
/********************************************************************
*********************************************************************/
int main( int argc, char *argv[] ) {
    int listenSockfd, newSockfd;
    struct addrinfo hints, *serverinfo, *tempP;
    struct sockaddr_storage clientAddr;
    socklen_t sockSize;
    int rv, numBytes;
    int yes = 1;
    char s[INET6_ADDRSTRLEN];
    FILE *fp;
    char buf[MAX_BUF_SIZE];

    openlog("aesdsocket", LOG_CONS, LOG_USER);

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;  //IPv4
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    // Get the addrinfo for binding socket
    if ((rv = getaddrinfo(NULL, SERVER_PORT, &hints, &serverinfo)) != 0) {
        syslog(LOG_ERR, "Failed to getaddrinfo with error %s", gai_strerror(rv));
        fprintf(stderr, "Failed to getaddrinfo with error %s", gai_strerror(rv));
        return -1;
    }

    // Could me multiple entries in the linked list server info loop through them all and bind first.
    for (tempP = serverinfo; tempP != NULL; tempP = tempP->ai_next) {
        if ((listenSockfd = socket(tempP->ai_family, tempP->ai_socktype,
            tempP->ai_protocol)) == -1) {
                syslog(LOG_ERR, "Failed to get socket with error %s", strerror(errno));
                perror("server: socket");
                continue;
        }

        if (setsockopt(listenSockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            syslog(LOG_ERR, "Failed to set socket options with error %s", strerror(errno));
            perror("setsockopt");
            return -1;
        }

        if (bind(listenSockfd, tempP->ai_addr, tempP->ai_addrlen) == -1) {
            close(listenSockfd);
            syslog(LOG_ERR, "Failed to bind socket with error %s", strerror(errno));
            perror("server: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(serverinfo);  // Now that we have either got bind or not this dynamic linked list is not needed.

    if (tempP == NULL) {
        syslog(LOG_ERR, "Failed to bind");
        fprintf(stderr, "server: failed to bind\n");
        return -1;
    }

    if (listen(listenSockfd, BACK_LOG) == -1) {
        syslog(LOG_ERR, "Failed to listen with error %s", strerror(errno));
        perror("listen");
        return -1;
    }

    // Setup temp file to log to.
    fp = fopen(TEMP_FILE, "w+");
    if( fp == NULL) {
        syslog(LOG_ERR, "Error opening file %s: %s\n", TEMP_FILE, strerror( errno ));
        printf("Failed to open file %s for storage.\n", TEMP_FILE);
    }

    syslog(LOG_INFO, "Waiting for connections");
    printf("Server: waiting for connections...\n");

    while(1) {
        // Accept incoming connections
        sockSize = sizeof clientAddr;
        newSockfd = accept(listenSockfd, (struct sockaddr *)&clientAddr, &sockSize);
        if (newSockfd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(clientAddr.ss_family, get_in_addr((struct sockaddr *)&clientAddr), s, sizeof s);
        printf("Accepted connection from %s\n",s);
        syslog(LOG_INFO, "Accepted connection from %s", s);

        // Recv data
        if ((numBytes = recv(newSockfd, &buf, MAX_BUF_SIZE-2, 0)) == -1) {
            syslog(LOG_ERR, "Recv error %s\n", strerror( errno ));
            perror("recv");
            return -1;
        }

        // Put in null char to terminate recv buf data
        buf[numBytes] = '\n';
        buf[numBytes + 1] = '\0';

        if ( fputs(buf, fp) == EOF ){
            syslog(LOG_ERR, "Failed to write to the storage file");
            printf("Failed to write to the storage file");
        }

        break;
    }

    close(listenSockfd);
    close(newSockfd);
    fclose(fp);
    closelog();

    return 0;
}