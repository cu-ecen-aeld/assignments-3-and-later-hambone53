#include <stdio.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Defines

// Types

// File Private Vars

// File private function prototypes

/********************************************************************
*********************************************************************/
int main( int argc, char *argv[] ) {
    int listenSockfd, newSockfd;
    struct addrinfo hints, *serverinfo, *tempP;

    openlog("aesdsocket", LOG_CONS, LOG_USER);

    syslog(LOG_ERR, "Testing initial logging!");

    closelog();

    return 0;
}