#define _GNU_SOURCE  /* for enabling asprintf */
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include "systemcalls.h"

#define PRINT_ERROR(msg)                                                   \
	do {                                                               \
		char *estr;                                                \
		asprintf(&estr, "[%s:%d] %s: %s (%d)",                     \
			 __FILE__, __LINE__, msg, strerror(errno), errno); \
		fprintf(stderr, "%s\n", estr);                             \
		fflush(stderr);                                            \
		free(estr);                                                \
	} while (0)

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
	int sysret = system(cmd);
	if (sysret < 0)
		return false;

	return true;
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

    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    // command[count] = command[count];

    va_end(args);

/*
 * TODO:
 *   Execute a system command by calling fork, execv(),
 *   and wait instead of system (see LSP page 161).
 *   Use the command[0] as the full path to the command to execute
 *   (first argument to execv), and use the remaining arguments
 *   as second argument to the execv() command.
 *
*/
	bool ret = true;
	int status;
	pid_t pid;

	pid = fork();
	switch (pid) {
	case -1: /* fork() has failed */
		PRINT_ERROR("fork");
		ret = false;
		break;
	case 0:	 /* child of successful fork() */
		if (execv(command[0], command) < 0) {
			PRINT_ERROR("execv");
			exit(EXIT_FAILURE);
		}
	}

	if (ret && (wait(&status) == -1)) {
		PRINT_ERROR("wait");
		ret = false;
	}

	if (ret && WIFEXITED(status))
		ret = !WEXITSTATUS(status);

	return ret;
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
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    //command[count] = command[count];

    va_end(args);

/*
 * TODO
 *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
 *   redirect standard out to a file specified by outputfile.
 *   The rest of the behaviour is same as do_exec()
 *
*/
	bool ret = true;
	int fd, status;
	pid_t pid;

	fd = open(outputfile, O_WRONLY|O_TRUNC|O_CREAT, 0644);
	if (fd < 0 ) {
		PRINT_ERROR("open");
		return false;
	}

	pid = fork();
	switch (pid) {
	case -1: /* fork() has failed */
		PRINT_ERROR("fork");
		ret = false;
		break;
	case 0:	 /* child of successful fork() */
		if (dup2(fd, 1) < 0) {
			PRINT_ERROR("dup2");
			close(fd);
			exit(EXIT_FAILURE);
		}

		if (execv(command[0], command) < 0) {
			PRINT_ERROR("execv");
			exit(EXIT_FAILURE);
		}
	}

	close(fd);

	if (ret && (wait(&status) == -1)) {
		PRINT_ERROR("wait");
		ret = false;
	}

	if (ret && WIFEXITED(status))
		ret = !WEXITSTATUS(status);

	return ret;
}
