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
#include <sys/queue.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>
#include "aesd_ioctl.h"

// Defines
#define USE_AESD_CHAR_DEVICE 1  // Set to 1 to use the char device and no timestamps, 0 to use file and timestamps
#define SERVER_PORT     "9000"
#define BACK_LOG        10
#define TEMP_FILE       "/var/tmp/aesdsocketdata"
#define AESD_DEVICE     "/dev/aesdchar"
#define MAX_BUF_SIZE    512
#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)
#define TIME_STAMP_SEC 10
#define AESD_CHAR_DEVICE_READ_SIZE 0x20000
#define IOCSEEKTO_CMD   "AESDCHAR_IOCSEEKTO:"

// Types
// SLIST.
typedef struct slist_data_s slist_data_t;
struct slist_data_s {
    pthread_t thread;
    pthread_mutex_t *file_mutex;
#if !defined(USE_AESD_CHAR_DEVICE)
    FILE *fp;
#else
    int fp;
#endif // USE_AESD_CHAR_DEVICE
    int socket;
    bool thread_complete_success;
    SLIST_ENTRY(slist_data_s) entries;
};

// File Private Vars
static int ShutdownNow = 0;

// File private function prototypes
char * recv_dynamic(int s);
void* threadfunc(void* thread_param);

#if !defined(USE_AESD_CHAR_DEVICE)
struct timer_thread_data
{
    FILE *fp;
    pthread_mutex_t *file_mutex;
};
#endif // USE_AESD_CHAR_DEVICE

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

#if !defined(USE_AESD_CHAR_DEVICE)
/**
* A thread which runs every timer_period_ms milliseconds
* Assumes timer_create has configured for sigval.sival_ptr to point to the
* thread data used for the timer
*/
static void timer_thread ( union sigval sigval )
{
    int mutex_rc, rc;
    struct timer_thread_data *td = (struct timer_thread_data*) sigval.sival_ptr;
    FILE *fp = td->fp;
    struct timespec ts_realtime;
    char timeString[200];
    char timeStamp[300];
    struct tm tm;

    mutex_rc = pthread_mutex_lock(td->file_mutex);

    if (mutex_rc != 0) {
        ERROR_LOG("Failed to acquire file mutex.");
        pthread_exit(NULL);
    }

    rc = clock_gettime(CLOCK_REALTIME, &ts_realtime);
    if (rc != 0) {
        ERROR_LOG("Failed to get realtime clock.");
    }

    if ( gmtime_r(&ts_realtime.tv_sec, &tm) == NULL ) {
        ERROR_LOG("Error calling gmtimer_r with time %ld",ts_realtime.tv_sec);
    } else {
        if ( strftime(timeString, sizeof(timeString), "%a, %d %b %y %T %z", &tm) == 0 ) {
            ERROR_LOG("Error converting string with strftime got: %i, %i, %i, %s", tm.tm_sec, tm.tm_year, tm.tm_mon, tm.tm_zone);
        }
    }

    sprintf(timeStamp, "timestamp:%s\n", timeString);

    if ( fputs(timeStamp, fp) == EOF ) {
        ERROR_LOG("Failed to write to the storage file");
    }

    pthread_mutex_unlock(td->file_mutex);
}

/**
* set @param result with @param ts_1 + @param ts_2
*/
static void timespec_add( struct timespec *result,
                        const struct timespec *ts_1, const struct timespec *ts_2)
{
    result->tv_sec = ts_1->tv_sec + ts_2->tv_sec;
    result->tv_nsec = ts_1->tv_nsec + ts_2->tv_nsec;
    if( result->tv_nsec > 1000000000L ) {
        result->tv_nsec -= 1000000000L;
        result->tv_sec ++;
    }
}

/**
* Setup the timer at @param timerid (previously created with timer_create) to fire
* every @param timer_period_ms milliseconds, using @param clock_id as the clock reference.
* The time now is saved in @param start_time
* @return true if the timer could be setup successfuly, false otherwise
*/
static bool setup_timer( int clock_id,
                         timer_t timerid, unsigned int timer_period_sec,
                         struct timespec *start_time)
{
    bool success = false;
    if ( clock_gettime(clock_id,start_time) != 0 ) {
        printf("Error %d (%s) getting clock %d time\n",errno,strerror(errno),clock_id);
    } else {
        struct itimerspec itimerspec;
        memset(&itimerspec, 0, sizeof(struct itimerspec));
        itimerspec.it_interval.tv_sec = timer_period_sec;
        itimerspec.it_interval.tv_nsec = 0;
        timespec_add(&itimerspec.it_value,start_time,&itimerspec.it_interval);
        if( timer_settime(timerid, TIMER_ABSTIME, &itimerspec, NULL ) != 0 ) {
            printf("Error %d (%s) setting timer\n",errno,strerror(errno));
            // printf("timer_settime args ")
        } else {
            success = true;
        }
    }
    return success;
}
#endif // USE_AESD_CHAR_DEVICE

/********************************************************************
*********************************************************************/
int main( int argc, char *argv[] ) {
    int listenSockfd, newSockfd;
    struct addrinfo hints, *serverinfo, *tempP;
    struct sockaddr_storage clientAddr;
    socklen_t sockSize;
    int rv;
    int yes = 1, run_as_daemon = 0;
    char s[INET6_ADDRSTRLEN];
#if !defined(USE_AESD_CHAR_DEVICE)
    FILE *fp;
#else
    int fp;
#endif // USE_AESD_CHAR_DEVICE
    struct sigaction new_action;
    slist_data_t *datap=NULL;
    pthread_mutex_t file_mutex;
#if !defined(USE_AESD_CHAR_DEVICE)
    struct sigevent sev;
    struct timer_thread_data td;
    timer_t timerid;
#endif // USE_AESD_CHAR_DEVICE

    openlog("aesdsocket", LOG_CONS, LOG_USER);

    // Init the linked list that will keep thread info
    SLIST_HEAD(slisthead, slist_data_s) head;
    SLIST_INIT(&head);

    if ( (rv = pthread_mutex_init(&file_mutex, NULL)) != 0) {
        syslog(LOG_ERR, "Error failed to init file mutex with code: %i", rv);
    }

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
        return -1;
    }

    if ( sigaction(SIGINT, &new_action, NULL) != 0 ) {
        syslog(LOG_ERR, "Error (%s) registering for SIGINT", strerror(errno));
        return -1;
    }

    if ( sigaction(SIGALRM, &new_action, NULL) != 0 ) {
        syslog(LOG_ERR, "Error (%s) registering for SIGALRM", strerror(errno));
        return -1;
    }

    // Get the addrinfo for binding socket
    if ((rv = getaddrinfo(NULL, SERVER_PORT, &hints, &serverinfo)) != 0) {
        syslog(LOG_ERR, "Failed to getaddrinfo with error %s", gai_strerror(rv));
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
#if !defined(USE_AESD_CHAR_DEVICE)
    fp = fopen(TEMP_FILE, "w+");
    if( fp == NULL) {
        syslog(LOG_ERR, "Error opening file %s: %s\n", TEMP_FILE, strerror( errno ));
    }
#else
    fp = 0;
    // fp = open(AESD_DEVICE, O_RDWR);
    // if( fp == -1) {
    //     syslog(LOG_ERR, "Error opening device %s: %s\n", AESD_DEVICE, strerror( errno ));
    // }
#endif // USE_AESD_CHAR_DEVICE

    syslog(LOG_INFO, "Waiting for connections");

#if !defined(USE_AESD_CHAR_DEVICE)
    /* Configure a 10 second timer */
    memset(&td, 0, sizeof(struct timer_thread_data));
    td.file_mutex = &file_mutex;
    td.fp = fp;
    memset(&sev, 0, sizeof(struct sigevent));
    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_value.sival_ptr = &td;
    sev.sigev_notify_function = timer_thread;

    int clock_id = CLOCK_MONOTONIC;
    struct timespec start_time;

    if ( timer_create(clock_id, &sev, &timerid) != 0 ) {
        syslog(LOG_ERR, "Failed to create time stamp timer.");
        ShutdownNow = 1;
    } else {
        if (!setup_timer(clock_id, timerid, TIME_STAMP_SEC, &start_time)) {
            syslog(LOG_ERR, "Failed to create time stamp timer.");
            ShutdownNow = 1;
        }
    }
#endif // USE_AESD_CHAR_DEVICE

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

        datap = malloc(sizeof(slist_data_t));
        datap->file_mutex = &file_mutex;
        datap->fp = fp;
        datap->socket = newSockfd;
        datap->thread_complete_success = false;

        SLIST_INSERT_HEAD(&head, datap, entries);

        rv = pthread_create(&datap->thread, NULL, threadfunc, (void *)datap);
        if (rv != 0) {
            syslog(LOG_ERR, "Failed to start thread.");
            ShutdownNow = 1;
            continue;
        }

        SLIST_FOREACH(datap, &head, entries) {
            if (datap->thread_complete_success) {
                if ( pthread_join(datap->thread, NULL) != 0 ) {
                    syslog(LOG_ERR, "Failed to join thread with error.");
                }
                SLIST_REMOVE(&head, datap, slist_data_s, entries);
                free(datap);
            }
        }
    }

    // Handle shutdown
    if (ShutdownNow) {
        syslog(LOG_INFO, "Caught signal, exiting");
        //printf("Caught signal, exiting\n");
        if (listenSockfd) {shutdown(listenSockfd, SHUT_RDWR);}
        if (newSockfd) {shutdown(newSockfd, SHUT_RDWR);}
        remove(TEMP_FILE);
        pthread_mutex_destroy(&file_mutex);
#if !defined(USE_AESD_CHAR_DEVICE)
        timer_delete(timerid);
        if (fp) {fclose(fp);};
#else
        if (fp) {close(fp);};
#endif // USE_AESD_CHAR_DEVICE

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

/*************************************************************************
 * ***********************************************************************/
void* threadfunc(void* thread_param) {
    slist_data_t* thread_func_args = (slist_data_t *) thread_param;
    int mutex_rc, numBytes;
    int socket = thread_func_args->socket;
    char *recvBuffer, *line;
#if !defined(USE_AESD_CHAR_DEVICE)
    FILE *fp = thread_func_args->fp;
    long fileWritePos;
    char newline = '\n';
#else
    int fp = thread_func_args->fp;
#endif // USE_AESD_CHAR_DEVICE


    // Recv data
    if (( recvBuffer = recv_dynamic(socket) ) == NULL) {
        ERROR_LOG("Got NULL when trying to recv.");
        thread_func_args->thread_complete_success = true;
        pthread_exit(NULL);
    }

    // Lock file and manipulate
    mutex_rc = pthread_mutex_lock(thread_func_args->file_mutex);

    if (mutex_rc != 0) {
        ERROR_LOG("Failed to acquire file mutex.");
        free(recvBuffer);
        thread_func_args->thread_complete_success = true;
        pthread_exit(NULL);
    }

#if !defined(USE_AESD_CHAR_DEVICE)
    if ( fputs(recvBuffer, fp) == EOF ) {
#else
    fp = open(AESD_DEVICE, O_RDWR);
    if( fp == -1) {
        ERROR_LOG("Error opening device %s: %s\n", AESD_DEVICE, strerror( errno ));
        free(recvBuffer);
        (void)pthread_mutex_unlock(thread_func_args->file_mutex);
        thread_func_args->thread_complete_success = true;
        pthread_exit(NULL);
    }

    // Check if the recvBuffer starts with the string as for the cmd IOCSEEKTO_CMD after that string the write_command is first integer before a comma and the write_cmd_offset is the second after the comma
    if ( strncmp(recvBuffer, IOCSEEKTO_CMD, strlen(IOCSEEKTO_CMD)) == 0 ) {
        char *write_command = strtok(recvBuffer + strlen(IOCSEEKTO_CMD), ",");
        char *write_cmd_offset = strtok(NULL, ",");
        if ( write_command == NULL || write_cmd_offset == NULL ) {
            ERROR_LOG("Failed to parse the write command and offset.");
            free(recvBuffer);
            (void)pthread_mutex_unlock(thread_func_args->file_mutex);
            thread_func_args->thread_complete_success = true;
            pthread_exit(NULL);
        }

        // Convert the write_command and write_cmd_offset to integers
        int write_command_int = atoi(write_command);
        int write_cmd_offset_int = atoi(write_cmd_offset);

        // Create aesd_seekto struct to pass to the device
        struct aesd_seekto seekto;
        seekto.write_cmd = write_command_int;
        seekto.write_cmd_offset = write_cmd_offset_int;

        // Using ioctl to seek to the write command and offset
        if ( ioctl(fp, AESDCHAR_IOCSEEKTO, &seekto) == -1 ) {
            ERROR_LOG("Failed to seek to the write command and offset.");
            free(recvBuffer);
            (void)pthread_mutex_unlock(thread_func_args->file_mutex);
            thread_func_args->thread_complete_success = true;
            pthread_exit(NULL);
        }

        //free the recvBuffer
        free(recvBuffer);
    } else {

        // Write the recvBuffer to the device as this was not a seek command
        if ( write(fp, recvBuffer, strlen(recvBuffer)) == -1 ) {
    #endif // USE_AESD_CHAR_DEVICE
            ERROR_LOG("Failed to write to the storage device.");
            free(recvBuffer);
            (void)pthread_mutex_unlock(thread_func_args->file_mutex);
            thread_func_args->thread_complete_success = true;
            pthread_exit(NULL);
        } else {
            free(recvBuffer);
        }
    }

#if !defined(USE_AESD_CHAR_DEVICE)
    // Need to reply with full content what we have in file storage.
    fileWritePos = ftell(fp);
    rewind(fp);

    while ( (line = read_line( fp )) ) {
        numBytes = strlen(line);

        if ( numBytes != 0) {
            if ( (sendAll(socket, line, &numBytes) == -1) ) {
                ERROR_LOG("Failed to send %i bytes to client!", numBytes);
                free(line);
                (void)pthread_mutex_unlock(thread_func_args->file_mutex);
                thread_func_args->thread_complete_success = true;
                pthread_exit(NULL);
            } else {
                free(line);
            }

            // Handle the need for new line
            numBytes = 1;
            if ( (sendAll(socket, &newline, &numBytes) == -1) ) {
                ERROR_LOG("Failed to send file to client!");
                (void)pthread_mutex_unlock(thread_func_args->file_mutex);
                thread_func_args->thread_complete_success = true;
                pthread_exit(NULL);
            }
        } else {
            free(line);
        }
    }
#else
    // Allocate memory for the line variable to read from fp and handle errors
    line = malloc(AESD_CHAR_DEVICE_READ_SIZE);

    if ( line == NULL ) {
        ERROR_LOG("Failed to allocate memory for reading from storage device.");
        (void)pthread_mutex_unlock(thread_func_args->file_mutex);
        thread_func_args->thread_complete_success = true;
        pthread_exit(NULL);
    }

    while ( (numBytes = read(fp, line, AESD_CHAR_DEVICE_READ_SIZE)) ) {
        if ( numBytes != 0) {
            if ( (sendAll(socket, line, &numBytes) == -1) ) {
                ERROR_LOG("Failed to send %i bytes to client!", numBytes);
                free(line);
                (void)pthread_mutex_unlock(thread_func_args->file_mutex);
                thread_func_args->thread_complete_success = true;
                pthread_exit(NULL);
            }
        }
    }

    if (line) {
        free(line);
    }

#endif // USE_AESD_CHAR_DEVICE

#if !defined(USE_AESD_CHAR_DEVICE)
    fseek(fp, fileWritePos, SEEK_SET);  // To position we were last writing at.
#else
    close(fp);
#endif // USE_AESD_CHAR_DEVICE
    close(socket);
    DEBUG_LOG("Closed connection from %i", socket);
    (void)pthread_mutex_unlock(thread_func_args->file_mutex);
    thread_func_args->thread_complete_success = true;
    pthread_exit(NULL);
}