#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <signal.h>
#include <getopt.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define CONN_BACKLOG	512
#define ARRAY_SIZE(a)	((int)(sizeof (a) / sizeof (__typeof__(a[0]))))

static bool signal_exit = false;
static int port = 9000;
const char *prog_name;
const char *file = "/var/tmp/aesdsocketdata";
const char *short_opts = "hdtp:f:";
const struct option long_opts[] = {
	{"help",	0, NULL, 'h'},
	{"daemon",	0, NULL, 'd'},
	{"term",	0, NULL, 't'},
	{"file",	1, NULL, 'f'},
	{"port",	1, NULL, 'p'},
	{NULL,		0, NULL,  0}
};
static int term_signals[] = {SIGINT, SIGCHLD, SIGTERM};

int handle_request(int socket_fd, FILE *stream);
char *read_line(int fd);

void print_usage(void)
{
	fprintf(stdout, "Usage: %s [-h] | [-p <#>] [-d] [-t] [-f </path/to/file>]\n", prog_name);
	fprintf(stdout, "    -h|--help                  Display this usage information.\n"
			"    -d|--daemon                Daemonize the server process.\n"
			"    -t|--term                  Write syslog messages to terminal.\n"
			"    -f|--file </path/to/file>  Change the location of data-file on " \
							"disk (default: %s).\n"
			"    -p|--port <#>              Change the port from default %d to #\n",
		        file, port);
	exit(0);
}

static void signal_handler(int signal)
{
	for (int i = 0; i < ARRAY_SIZE(term_signals); i++) {
		if (signal == term_signals[i]) {
			signal_exit = true;
			break;
		}
	}
}

void panic(const char *msg, int error)
{
	syslog(LOG_ERR, "ERROR: %s%c %s\n", msg,
		(error) ? ':' : ' ',
		(error) ? strerror(error) : " ");

	exit(-1);
}

int main(int argc, char *argv[])
{
	struct sockaddr_in socket_addr;
	struct sigaction sa;
	int next_opt, socket_fd;
	int log_options = LOG_PID;
	bool daemonize = false;
	FILE *stream;

	prog_name = argv[0];

	do {
		next_opt = getopt_long(argc, argv, short_opts, long_opts, NULL);
		switch (next_opt) {
			case 'h':
			case '?':
				print_usage();
				break;
			case 'd':
				daemonize = true;
				break;
			case 't':
				log_options |= LOG_PERROR;
				break;
			case 'f':
				file = optarg;
				break;
			case 'p':
				port = atoi(optarg);
			case -1:
				break;
			default:
				abort();
		}
	} while (next_opt != -1);

	if (daemonize) {
		log_options = LOG_PID;

		/* ignore SIGHUP in daemon mode */
		memset(&sa, 0, sizeof (sa));
		sa.sa_handler = SIG_IGN;
		if (sigaction(SIGHUP, &sa, NULL) < 0)
			panic("sigaction()", errno);

		/* see daemon(3) man page for more details */
		if (daemon(0, 0) < 0)
			panic("daemon()", errno);
	}

	openlog(NULL, log_options, LOG_USER);

	/* open/create the data file */
	stream = fopen(file, "a+");
	if (!stream)
		panic("fopen()", errno);

	/* set up signal handler actions for catching common termination signals */
	memset(&sa, 0, sizeof (sa));
	sa.sa_handler = signal_handler;
	for (int i = 0; i < ARRAY_SIZE(term_signals); i++) {
		if (sigaction(term_signals[i], &sa, NULL) < 0)
			panic("sigaction()", errno);
	}

	/* set up the server socket */
	socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (socket_fd < 0)
		panic("socket()", errno);

	/* allow reusing addrs still in TIME_WAIT for quick respawns of this server */
	if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof (int)) < 0)
		panic("setsockopt()", errno);

	memset(&socket_addr, 0, sizeof (socket_addr));
	socket_addr.sin_family = AF_INET;
	socket_addr.sin_port = htons(port);
	socket_addr.sin_addr.s_addr = INADDR_ANY;
	if (bind(socket_fd, (struct sockaddr *)&socket_addr, sizeof (socket_addr)) < 0)
		panic("bind()", errno);

	if (listen(socket_fd, CONN_BACKLOG) < 0)
		panic("bind()", errno);

	syslog(LOG_INFO, "server listening at %s:%u",
			inet_ntoa(socket_addr.sin_addr),
			(unsigned)ntohs(socket_addr.sin_port));

	/*
	 * server is set and running. now we get ready to handle
	 * incoming requests sequentially (one child at a time),
	 * as requested by the implementation instructions.
	 */
	for (;;) {
		int rc, request_fd = accept(socket_fd, NULL, NULL);
		if (request_fd < 0) {
			switch (errno) {
				case EINTR:
					goto signal_out;
				default:
					panic("accept()", errno);
			}
		}

		rc = handle_request(request_fd, stream);
		if (rc < 0)
			panic("handle_request", errno);

		close(request_fd);
signal_out:
		if (signal_exit) {
			syslog(LOG_INFO, "Caught signal, exiting");
			break;
		}
	}

	/*
	 * a signal was caught, and we broke out of the server loop.
	 * so we gracefully wrap up and terminate the main program.
	 */
	close(socket_fd);
	fclose(stream);
	unlink(file);
	closelog();

	return 0;
}

int handle_request(int socket_fd, FILE *stream)
{
	struct sockaddr_in peer_addr;
	socklen_t socket_len = 0;
	char *line = NULL;
	size_t len = 0;
	ssize_t nread;

	memset(&peer_addr, 0, sizeof (peer_addr));
	if (getpeername(socket_fd, (struct sockaddr *)&peer_addr, &socket_len) < 0)
		return EXIT_FAILURE;

	syslog(LOG_INFO, "Accepted connection from %s",
			inet_ntoa(peer_addr.sin_addr));

	/* get a line from the socket and write it to file */
	if ((line = read_line(socket_fd)) == NULL)
		return EXIT_FAILURE;

	fprintf(stream, "%s\n", line);
	fflush(stream);
	free(line);

	/* read file and send its whole content back through the socket */
	line = NULL;
	fseek(stream, 0, SEEK_SET);
	while ((nread = getline(&line, &len, stream)) != -1) {
		ssize_t nwrite = 0;

		for (ssize_t written = 0; written < nread; written += nwrite) {
			nwrite = write(socket_fd, line+written, nread-written);
			if (nwrite < 0 && errno == EINTR)
				continue;
			else if (nwrite < 0)
				panic("write()", errno);
		}
	}

	free(line);
	syslog(LOG_INFO, "Closed connection from %s",
			inet_ntoa(peer_addr.sin_addr));

	return EXIT_SUCCESS;
}

char *read_line(int fd)
{
	ssize_t bytes, buffer_size = 65536, offset = 0;
	char *line = NULL;

	line = calloc(buffer_size, sizeof (line));
	if (line == NULL)
		goto error_out;

	while ((bytes = read(fd, line+offset, 1024)) > 0) {
		char *pos = NULL;

		if ((pos = strchr(line+offset, '\n')) != NULL) {
			*pos = '\0';
			break;
		}

		if ((offset + bytes) >= buffer_size) {
			buffer_size *= 2;
			line = realloc(line, buffer_size);
			if (line == NULL)
				goto error_out;
		}

		offset += bytes;
	}

error_out:
	return line;
}

