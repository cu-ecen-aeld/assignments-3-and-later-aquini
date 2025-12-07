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
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>

#include "slist.h"
#include "../aesd-char-driver/aesd_ioctl.h"

#define CONN_BACKLOG	512
#define ARRAY_SIZE(a)	((int)(sizeof (a) / sizeof (__typeof__(a[0]))))
#define __maybe_unused __attribute__((unused))

pthread_mutex_t log_write_mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned long sequence = 0;
static bool signal_exit = false;
static bool do_work = false;
static int port = 9000;
const char *prog_name;
#ifdef USE_AESD_CHAR_DEVICE
const char *file = "/dev/aesdchar";
#else
const char *file = "/var/tmp/aesdsocketdata";
#endif
const char *short_opts = "hdp:f:";
const struct option long_opts[] = {
	{"help",	0, NULL, 'h'},
	{"daemon",	0, NULL, 'd'},
	{"file",	1, NULL, 'f'},
	{"port",	1, NULL, 'p'},
	{NULL,		0, NULL,  0}
};
static int term_signals[] = {SIGINT, SIGCHLD, SIGTERM};

struct thread_desc {
	pthread_t id;
	bool done;
	void *data[2];
	SLIST_ENTRY(thread_desc) list;
};

int handle_request(int socket_fd, FILE *stream);
char *read_line(int fd);
void *monitor_worker(void *arg);
void *request_worker(void *arg);
void write_timestamp(FILE *stream);

void print_usage(void)
{
	fprintf(stdout, "Usage: %s [-h] | [-p <#>] [-d] [-f </path/to/file>]\n", prog_name);
	fprintf(stdout, "    -h|--help                  Display this usage information.\n"
			"    -d|--daemon                Daemonize the server process.\n"
			"    -f|--file </path/to/file>  Change the location of data-file on " \
							"disk (default: %s).\n"
			"    -p|--port <#>              Change the port from default %d to #\n",
		        file, port);
	exit(0);
}

static void sequencer(int signal __maybe_unused)
{
	sequence += 1;
	do_work = true;
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

void warn(const char *msg, int error)
{
	syslog(LOG_ERR, "ERROR: %s%c %s\n", msg,
		(error) ? ':' : ' ',
		(error) ? strerror(error) : " ");
}

int main(int argc, char *argv[])
{
	struct sockaddr_in socket_addr;
	struct sigaction sa;
	int rc, next_opt, socket_fd;
	struct itimerspec last_its, its = {{1, 0}, {1, 0}};
	timer_t timerid;
	bool daemonize = false;
	FILE *stream;
	struct thread_desc *monitor;
	SLIST_HEAD(slisthead, thread_desc) threads;

	prog_name = argv[0];
	SLIST_INIT(&threads);

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

	openlog(NULL, LOG_PID|LOG_PERROR, LOG_USER);

	if (daemonize) {
		/* ignore SIGHUP in daemon mode */
		memset(&sa, 0, sizeof (sa));
		sa.sa_handler = SIG_IGN;
		if (sigaction(SIGHUP, &sa, NULL) < 0)
			panic("sigaction()", errno);

		/* see daemon(3) man page for more details */
		if (daemon(0, 0) < 0)
			panic("daemon()", errno);
	}

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

	/* set up a timer to sequence the monitor thread work */
	memset(&sa, 0, sizeof (sa));
	sa.sa_handler = sequencer;
	if (sigaction(SIGALRM, &sa, NULL) < 0)
		panic("sigaction()", errno);

	timer_create(CLOCK_REALTIME, NULL, &timerid);
	timer_settime(timerid, 0, &its, &last_its);

	/* set up a monitor thread, responsible for all required janitorial tasks */
	monitor = calloc(1, sizeof (*monitor));
	if (!monitor)
		panic("calloc()", errno);

	monitor->data[0] = stream;
	monitor->done = false;
	rc = pthread_create(&monitor->id, NULL, monitor_worker, monitor);
	if (rc != 0)
		panic("pthread_create()", errno);

	SLIST_INSERT_HEAD(&threads, monitor, list);

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

	/*
	 * server is set and running. now we get ready to handle
	 * incoming requests concurrently (one thread per connection),
	 * as requested by the implementation instructions.
	 */
	for (;;) {
		struct thread_desc *client;
		int request_fd;

		request_fd = accept(socket_fd, NULL, NULL);
		if (request_fd < 0) {
			switch (errno) {
				case EINTR:
					goto signal_out;
				default:
					panic("accept()", errno);
			}
		}

		client = calloc(1, sizeof (*client));
		if (!client) {
			warn("calloc()", errno);
			continue;
		}

		rc = pthread_create(&client->id, NULL, request_worker, client);
		if (rc != 0) {
			warn("pthread_create()", errno);
			free(client);
			continue;
		}

		client->data[0] = stream;
		client->data[1] = (void *)(unsigned long)request_fd;
		client->done = false;
		SLIST_INSERT_HEAD(&threads, client, list);
signal_out:
		if (signal_exit) {
			syslog(LOG_INFO, "Caught signal, exiting");
			break;
		}
	}

	/*
	 * a signal, or other exception, was caught and we broke out
	 * of the server loop.
	 * So, we gracefully wrap up and terminate the main program.
	 */
	while (!SLIST_EMPTY(&threads)) {
		struct thread_desc *t, *tmp;

		SLIST_FOREACH_SAFE(t, &threads, list, tmp) {
			if (t->done) {
				rc = pthread_join(t->id, NULL);
				if (rc != 0)
					panic("pthread_join()", errno);

				SLIST_REMOVE(&threads, t, thread_desc, list);
				free(t);
			}
		}
	}

	shutdown(socket_fd, SHUT_RDWR);
	close(socket_fd);
#ifndef USE_AESD_CHAR_DEVICE
	fclose(stream);
#endif
	unlink(file);
	closelog();

	return 0;
}

void write_timestamp(FILE *stream)
{
	char str[512];
	struct tm *tm_info;
	time_t t;

	time(&t);
	tm_info = localtime(&t);
	strftime(str, sizeof (str), "timestamp: %a, %d %b %Y %T %z", tm_info);
	fprintf(stream, "%s\n", str);
	fflush(stream);
}

void *monitor_worker(void *arg)
{
	struct thread_desc *desc = arg;

	for (;;) {
		if (signal_exit)
			break;

		while (!do_work)
			sleep(1);

		do_work = false;

		/* janitorial work here */
#ifndef USE_AESD_CHAR_DEVICE
		if (!(sequence % 10)) {
			FILE *stream = desc->data[0];
			pthread_mutex_lock(&log_write_mutex);
			write_timestamp(stream);
			pthread_mutex_unlock(&log_write_mutex);
		}
#endif
	}

	desc->done = true;

	return NULL;
}

void *request_worker(void *arg)
{
	struct thread_desc *desc = arg;
	FILE *stream = desc->data[0];
	int rc, socket_fd = (unsigned long)desc->data[1];

	rc = handle_request(socket_fd, stream);
	if (rc < 0)
		warn("handle_request", errno);

	shutdown(socket_fd, SHUT_RDWR);
	close(socket_fd);

	desc->done = true;

	return NULL;
}

void echo(int socket_fd, FILE *stream)
{
	char *line = NULL;
	size_t len = 0;
	ssize_t nread;

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
}

int handle_request(int socket_fd, FILE *stream)
{
	struct sockaddr_in peer_addr;
	socklen_t socket_len = 0;
	char *line = NULL;
	struct aesd_seekto seekto __maybe_unused;

	memset(&peer_addr, 0, sizeof (peer_addr));
	if (getpeername(socket_fd, (struct sockaddr *)&peer_addr, &socket_len) < 0)
		return EXIT_FAILURE;

	syslog(LOG_INFO, "Accepted connection from %s",
			inet_ntoa(peer_addr.sin_addr));

	/* get a line from the socket */
	if ((line = read_line(socket_fd)) == NULL)
		return EXIT_FAILURE;

	pthread_mutex_lock(&log_write_mutex);
#ifdef USE_AESD_CHAR_DEVICE
	/* parse AESDCHAR_IOCSEEKTO:n,n */
	if (sscanf(line, "AESDCHAR_IOCSEEKTO:%u,%u\n", &seekto.write_cmd,
	    &seekto.write_cmd_offset) == 2) {
		int fd, offset;

		fseek(stream, 0, SEEK_SET);
		syslog(LOG_INFO, "received AESDCHAR_IOCSEEKTO:%u,%u",
			seekto.write_cmd, seekto.write_cmd_offset);

		if ((fd = fileno(stream)) < 0)
			panic("fileno()", errno);

		if ((offset = ioctl(fd, AESDCHAR_IOCSEEKTO, &seekto)) < 0)
			panic("ioctl()", errno);

		free(line);
		echo(socket_fd, stream);
		pthread_mutex_unlock(&log_write_mutex);
		syslog(LOG_INFO, "Closed connection from %s",
			inet_ntoa(peer_addr.sin_addr));

		return EXIT_SUCCESS;
	}
#endif

	/* write line gotten from the socket into the file */
	fprintf(stream, "%s\n", line);
	fflush(stream);
	free(line);

	/* echo the whole file back to the socket */
	fseek(stream, 0, SEEK_SET);
	echo(socket_fd, stream);
	pthread_mutex_unlock(&log_write_mutex);

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

