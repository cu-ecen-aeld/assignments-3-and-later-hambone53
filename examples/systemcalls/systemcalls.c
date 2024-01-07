#include "systemcalls.h"
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{

/*
 * TODO  add your code here
 *  Call the system() function with the command set in the cmd
 *   and return a boolean true if the system() call completed with success
 *   or false() if it returned a failure
*/
    if (cmd == NULL) {
        return false;
    }

    if (WIFEXITED(system(cmd))) {
        return true;
    } else {
        return false;
    }
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    int childPID, wstatus, exeReturn;

    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

/*
 * TODO:
 *   Execute a system command by calling fork, execv(),
 *   and wait instead of system (see LSP page 161).
 *   Use the command[0] as the full path to the command to execute
 *   (first argument to execv), and use the remaining arguments
 *   as second argument to the execv() command.
 *
*/
    fflush(stdout);

    printf("Forking\n");
    if ((childPID = fork()) == -1) {
        //failed to fork
        printf("Fork failed\n");
        return false;
    }
    else if (childPID == 0) {
        // Child Process
        printf("Child process about to run command\n");
        if ((exeReturn = execv(command[0], command)) == -1) {
            // Failed to run execv syscall
            printf("execv command failed to run\n");
            _exit(1);
        }
    }
    else {
        // Parent Process
        printf("Parent process waiting\n");
        if (wait(&wstatus) == -1) {
            // Wait failed
            printf("Wait failed\n");
            return false;
        }
        else {
            if (WIFEXITED(wstatus) != 0) {
                // Child process ended correctly
                // Check the status to see if it failed to execute the command
                if (WEXITSTATUS(wstatus) != 0) {
                    printf("Child process existed but did not run command\n");
                    return false;
                }
            }
            else {
                // Child process did not end correctly
                return false;
            }
        }
    }

    va_end(args);

    printf("Hit final return\n");
    return true;
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i, childPID, wstatus;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;


/*
 * TODO
 *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
 *   redirect standard out to a file specified by outputfile.
 *   The rest of the behaviour is same as do_exec()
 *
*/
    int fd = open(outputfile, O_WRONLY|O_TRUNC|O_CREAT, 0644);
    if (fd < 0) {
        printf("Failed to open redirect file.\n");
        return false;
    }

    fflush(stdout);

    printf("Forking\n");
    if ((childPID = fork()) == -1) {
        //failed to fork
        printf("Fork failed\n");
        return false;
    }
    else if (childPID == 0) {
        // Child Process
        printf("Child process about to run command\n");

        if (dup2(fd, 1) < 0) {
            printf("Failed to duplicate file descriptor\n");
            _exit(1);
        }
        close(fd);
        if (execv(command[0], command) == -1) {
            // Failed to run execv syscall
            printf("execv command failed to run\n");
            _exit(1);
        }
    }
    else {
        // Parent Process
        printf("Parent process waiting\n");
        close(fd);
        if (wait(&wstatus) == -1) {
            // Wait failed
            printf("Wait failed\n");
            return false;
        }
        else {
            if (WIFEXITED(wstatus) != 0) {
                // Child process ended correctly
                // Check the status to see if it failed to execute the command
                if (WEXITSTATUS(wstatus) != 0) {
                    printf("Child process existed but did not run command\n");
                    return false;
                }
            }
            else {
                // Child process did not end correctly
                return false;
            }
        }
    }

    va_end(args);

    printf("Hit final return\n");
    return true;
}
