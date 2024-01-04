#include <stdio.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>

// DEFINES

// TYPES

// File Private Vars

// File Private Functions Prototypes

/**************************************************************
 * ************************************************************/
int main( int argc, char *argv[] ) {
    FILE *fp;
    char *fileName;
    char *stringToWrite;

    openlog("writer", LOG_CONS, LOG_USER);

    if( argc != 3) {
        syslog(LOG_ERR, "Incorrect number of arguments. Should include file to write and string to write to file.");
        closelog();
        return 1;
    }

    fileName = argv[1];
    stringToWrite = argv[2];

    // Attempt to open the specified file with overwriting.
    fp = fopen(fileName, "w+");
    if( fp == NULL) {
        syslog(LOG_ERR, "Error opening file %s: %s\n", fileName, strerror( errno ));
        closelog();
        return 1;
    }

    // Write to file and close out.
    if( fputs(stringToWrite, fp) == EOF ){
        syslog(LOG_ERR, "Failed to write string %s to file %s", stringToWrite, fileName);
        fclose(fp);
        closelog();
        return 1;
    }

    syslog(LOG_DEBUG, "Writing %s to %s", stringToWrite, fileName);

    fclose(fp);
    closelog();
    
    return 0;
}