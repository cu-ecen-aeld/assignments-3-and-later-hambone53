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
#include <signal.h>
#include <fcntl.h>

// Defines
#define SERVER_PORT     "9000"
#define BACK_LOG        10
#define TEMP_FILE       "/var/tmp/aesdsocketdata"
#define MAX_BUF_SIZE    512

// Types

// File Private Vars
static int ShutdownNow = 0;

// File private function prototypes
char * recv_dynamic(int s);

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
// Read a line from a file.
char * read_line( FILE * f ) {
    int cap = 32, next = 0, c;
    char * ptrLine = malloc(cap);

    while( 1 ) { 
        if ( next == cap ) {
            ptrLine = realloc( ptrLine, cap *= 2 );
        }
        c = fgetc( f ); 
        if ( c == EOF || c == '\n' ) {
            ptrLine[next++] = 0;
            break;
        }
        ptrLine[next++] = c;
    }

    // Handle an empty file.
    if ( c == EOF && next == 1 ) {
        free( ptrLine );
        ptrLine = NULL;
    }

    return ptrLine;
}

/********************************************************************
Send all content of a buffer
*********************************************************************/
int sendAll(int s, char *buf, int *len) {
    int total = 0;        // how many bytes have been sent
    int bytesleft = *len; // how many we have left to send
    int n;

    while(total < *len) {
        n = send(s, buf+total, bytesleft, 0);
        if (n == -1) { break; }
        total += n;
        bytesleft -= n;
    }

    *len = total;       // return the total number sent
    return n==-1?-1:0; // return -1 if we encountered a send error.
}

/********************************************************************
Signal handler
*********************************************************************/
void signal_handler(int s) {
    if ( s == SIGINT  || s == SIGTERM) {
        ShutdownNow = 1;
    }
}


/********************************************************************
*********************************************************************/
int main( int argc, char *argv[] ) {
    int listenSockfd, newSockfd;
    struct addrinfo hints, *serverinfo, *tempP;
    struct sockaddr_storage clientAddr;
    socklen_t sockSize;
    int rv, numBytes;
    int yes = 1, run_as_daemon = 0;
    char s[INET6_ADDRSTRLEN], newline = '\n';
    FILE *fp;
    char *recvBuffer, *line;
    long fileWritePos;
    struct sigaction new_action;

    openlog("aesdsocket", LOG_CONS, LOG_USER);

    // Check if we should run as a daemon
    if ( argc >= 2 ) {
        if (strcmp(argv[1],"-d") == 0) {
            run_as_daemon = 1;
            //printf("Going to run as daemon.\n");
        }
    }

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;  //IPv4
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    // Setup signal handling
    memset(&new_action, 0, sizeof(struct sigaction));
    new_action.sa_handler = signal_handler;

    if ( sigaction(SIGTERM, &new_action, NULL) != 0 ) {
        syslog(LOG_ERR, "Error (%s) registering for SIGTERM", strerror(errno));
        fprintf(stderr, "Error (%s) registering for SIGTERM", strerror(errno));
        return -1;
    }

    if ( sigaction(SIGINT, &new_action, NULL) != 0 ) {
        syslog(LOG_ERR, "Error (%s) registering for SIGINT", strerror(errno));
        fprintf(stderr, "Error (%s) registering for SIGINT", strerror(errno));
        return -1;
    }

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

    if (run_as_daemon) {
        if (fork()) { // This should start a child process and if we get a return then we are parent
            return 0;
        }
    }

    if (listen(listenSockfd, BACK_LOG) == -1) {
        syslog(LOG_ERR, "Failed to listen with error %s", strerror(errno));
        perror("listen");
        return -1;
    }

    // Setup temp file to log to cleaning out whatever is there already.
    fp = fopen(TEMP_FILE, "w+");
    if( fp == NULL) {
        syslog(LOG_ERR, "Error opening file %s: %s\n", TEMP_FILE, strerror( errno ));
        printf("Failed to open file %s for storage.\n", TEMP_FILE);
    }

    syslog(LOG_INFO, "Waiting for connections");
    //printf("Server: waiting for connections...\n");

    while(!ShutdownNow) {
        // Accept incoming connections
        sockSize = sizeof clientAddr;
        newSockfd = accept(listenSockfd, (struct sockaddr *)&clientAddr, &sockSize);
        if (newSockfd == -1) {
            //perror("accept");
            continue;
        }

        inet_ntop(clientAddr.ss_family, get_in_addr((struct sockaddr *)&clientAddr), s, sizeof s);
        //printf("Accepted connection from %s\n",s);
        syslog(LOG_INFO, "Accepted connection from %s", s);

        // Recv data
        if (( recvBuffer = recv_dynamic(newSockfd) ) == NULL) {
            printf("Got NULL when trying to recv\n");
            ShutdownNow = 1;
            continue;
        }

        if ( fputs(recvBuffer, fp) == EOF ) {
            syslog(LOG_ERR, "Failed to write to the storage file");
            printf("Failed to write to the storage file");
        }

        free(recvBuffer);
        
        // Need to reply with full content what we have in file storage.
        fileWritePos = ftell(fp);
        rewind(fp);

        // Note: read_line will malloc a new char pointer so need to free when done.
        while ( (line = read_line( fp )) ) {
            numBytes = strlen(line);

            if ( numBytes != 0) {
                if ( (sendAll(newSockfd, line, &numBytes) == -1) ) {
                    syslog(LOG_ERR, "Failed to send file to client!");
                    printf("Failed to send file to client");
                    free(line);
                    return -1;
                }


                free(line);

                // Handle the need for new line
                numBytes = 1;
                if ( (sendAll(newSockfd, &newline, &numBytes) == -1) ) {
                    syslog(LOG_ERR, "Failed to send file to client!");
                    printf("Failed to send file to client");
                    return -1;
                }
            }

        }

        fseek(fp, fileWritePos, SEEK_SET);  // To position we were last writing at.
        close(newSockfd);
        //printf("Closed connection from %s\n",s);
        syslog(LOG_INFO, "Closed connection from %s", s);

    }

    // Handle shutdown
    if (ShutdownNow) {
        syslog(LOG_INFO, "Caught signal, exiting");
        //printf("Caught signal, exiting\n");
        if (listenSockfd) {shutdown(listenSockfd, SHUT_RDWR);}
        if (newSockfd) {shutdown(newSockfd, SHUT_RDWR);}
        if (fp) {fclose(fp);}
        remove(TEMP_FILE);
        closelog();
        return 0;
    }

    return 0;
}

/********************************************************************
Recieve every bit of data
*********************************************************************/
char * recv_dynamic(int s) {
    int size_recv = 0, pos = 0;
    char chunk[MAX_BUF_SIZE];
    char *p = malloc(MAX_BUF_SIZE);

    // Make socket no blocking
    fcntl(s, F_SETFL, O_NONBLOCK);

    while(1) {
        // Check shutdown
        if (ShutdownNow) {
            free(p);
            return NULL;
        }

        // recv up to the buffer size
        memset(chunk, 0, MAX_BUF_SIZE);

        if ( (size_recv = recv(s, &chunk, MAX_BUF_SIZE, 0)) == -1 ) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                usleep(1000000); // No data backoff for 1msec
                continue;
            }
            else {
                //Another error occured
                syslog(LOG_ERR, "Recv error %s\n", strerror( errno ));
                free(p);
                return NULL;
            }
        }


        if (size_recv > 0) {
            // printf("Recieved this much data %d\n", size_recv);
            memcpy(&p[pos], &chunk, size_recv);
            // printf("The character at end is %d\n", p[(pos + size_recv)-1]);
            // check if last character is newline then jump out
            if ( p[pos + size_recv - 1] == '\n' ) {
                p[pos + size_recv] = 0; //Null terminate the string.
                break;
            }
            // else reallocate another chunk and recv more
            pos = pos + size_recv;
            // printf("Current recv pos: %d\n", pos);
            // printf("Reallocating %d\n", pos + (2 *MAX_BUF_SIZE));
            p = realloc(p, pos + (2 *MAX_BUF_SIZE));
        }
    }

    return p;
}