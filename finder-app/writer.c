/*
 * writer.c: re-implementation of assignment1 writer.sh
 * 	     as requested for assignment2.
 * Copyright (c) 2025, Rafael Aquini <raaquini@gmail.com>
 */
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

int main(int argc, char *argv[])
{
	FILE *file;
	const char *str = argv[2];

	openlog(NULL, LOG_PERROR|LOG_PID, LOG_USER);

	if (argc != 3) {
		syslog(LOG_ERR, "ERROR: illegal number of arguments: %d/2\n", argc - 1);
		return EXIT_FAILURE;
	}

	if (!strlen(str)) {
		syslog(LOG_ERR, "ERROR: write string is empty\n");
		return EXIT_FAILURE;
	}

	file = fopen(argv[1], "w+");
	if (!file) {
		syslog(LOG_ERR, "ERROR: %s: %s\n", argv[1], strerror(errno));
		return EXIT_FAILURE;
	}

	syslog(LOG_DEBUG, "Writing \"%s\" to %s\n", str, argv[1]);
	fprintf(file, "%s\n", str);

	fclose(file);
	closelog();
	return EXIT_SUCCESS;
}
