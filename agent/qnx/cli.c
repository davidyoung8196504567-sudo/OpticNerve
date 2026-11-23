/*
 * cli.c: the subcommands that talk to a running agent (private design 4.10). Each runs as the calling user and holds
 * no privilege of its own; stop and passwd are signals, which the system allows to root only.
 */
#define _QNX_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/neutrino.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "on_dev.h"

#define LIMIT_S 5 /* every request has a limit, so a stuck agent does not hang its callers */

static int open_dev(void)
{
	int fd = open(ON_DEV_PATH, O_RDONLY);
	if (fd < 0 && errno != EINTR)
		fprintf(stderr, "opticnerve: %s: %s (is the agent running?)\n", ON_DEV_PATH, strerror(errno));
	return fd;
}

static void on_alarm(int sig)
{
	static const char msg[] = "opticnerve: agent not answering\n";
	(void)sig;
	(void)!write(2, msg, sizeof(msg) - 1); /* write() and _exit() are safe in a signal handler */
	_exit(3);
}

/* The limit covers the whole request, open() included. An interrupted call is not enough: on the Pi, status
 * stayed blocked against a stopped agent with the alarm only interrupting (2026-11-16), so the handler ends the
 * subcommand itself. */
static void limit(unsigned seconds)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_alarm;
	sigaction(SIGALRM, &sa, NULL);
	alarm(seconds);
}

static int not_answering(void)
{
	fprintf(stderr, "opticnerve: agent not answering\n");
	return 3;
}

/* Reads the whole status text into buf. Returns its length, or -1. */
static int read_status(char *buf, size_t cap)
{
	limit(LIMIT_S);
	int fd = open(ON_DEV_PATH, O_RDONLY);
	if (fd < 0) {
		alarm(0);
		if (errno == EINTR)
			return -2;
		fprintf(stderr, "opticnerve: %s: %s (is the agent running?)\n", ON_DEV_PATH, strerror(errno));
		return -1;
	}
	size_t n = 0;
	for (;;) {
		ssize_t r = read(fd, buf + n, cap - 1 - n);
		if (r < 0) {
			alarm(0);
			close(fd);
			return errno == EINTR ? -2 : -1;
		}
		if (r == 0 || n + (size_t)r >= cap - 1) {
			n += (size_t)(r > 0 ? r : 0);
			break;
		}
		n += (size_t)r;
	}
	alarm(0);
	close(fd);
	buf[n] = 0;
	return (int)n;
}

static int cmd_status(void)
{
	char buf[8192];
	int n = read_status(buf, sizeof(buf));
	if (n == -2)
		return not_answering();
	if (n < 0)
		return 1;
	fputs(buf, stdout);
	return 0;
}

static long opt(int argc, char **argv, const char *name, long dflt)
{
	size_t l = strlen(name);
	for (int i = 0; i < argc; i++)
		if (strncmp(argv[i], name, l) == 0 && argv[i][l] == '=')
			return strtol(argv[i] + l + 1, NULL, 10);
	return dflt;
}

static int cmd_wait(int argc, char **argv)
{
	struct on_dev_wait w = { .since = opt(argc, argv, "-since", -1),
				 .timeout_ms = (int32_t)(opt(argc, argv, "-timeout", 2) * 1000),
				 .quiet_ms = (int32_t)opt(argc, argv, "-quiet", 0) };
	limit((unsigned)(w.timeout_ms / 1000) + 1 + LIMIT_S);
	int fd = open_dev();
	if (fd < 0)
		return errno == EINTR ? not_answering() : 1;
	int rc = devctl(fd, ON_DCMD_WAIT, &w, sizeof(w), NULL);
	alarm(0);
	close(fd);
	if (rc == EINTR)
		return not_answering();
	if (rc != EOK) {
		fprintf(stderr, "opticnerve: wait: %s\n", strerror(rc));
		return 1;
	}
	static const char *what[] = { "change", "quiet", "timeout" };
	printf("%s %llu\n", what[w.result], (unsigned long long)w.count);
	return w.result == ON_WAIT_TIMEOUT ? 2 : 0; /* the exit status tells a change from a timeout */
}

static int cmd_current(void)
{
	struct on_dev_current c;
	memset(&c, 0, sizeof(c));
	limit(2 * LIMIT_S);
	int fd = open_dev();
	if (fd < 0)
		return errno == EINTR ? not_answering() : 1;
	int rc = devctl(fd, ON_DCMD_CURRENT, &c, sizeof(c), NULL);
	alarm(0);
	close(fd);
	if (rc == EINTR)
		return not_answering();
	if (rc != EOK) {
		fprintf(stderr, "opticnerve: current: %s\n", strerror(rc));
		return 1;
	}
	printf("content %llu\n", (unsigned long long)c.content);
	int all = 1;
	for (int i = 0; i < 6; i++)
		if (c.state[i]) {
			printf("slot %d %s marked_tiles %d queued %u\n", i, c.state[i] == 1 ? "current" : "not-current",
			       c.marked[i], c.queued[i]);
			all = all && c.state[i] == 1;
		}
	return all ? 0 : 2;
}

/* capture X Y W H FILE: the agent captures; this process writes FILE itself, as the calling user, as PPM (P6). */
static int cmd_capture(int argc, char **argv)
{
	if (argc != 5) {
		fprintf(stderr, "usage: opticnerve capture X Y W H FILE\n");
		return 2;
	}
	struct on_dev_snapshot s = { atoi(argv[0]), atoi(argv[1]), atoi(argv[2]), atoi(argv[3]), 0, 0, 0 };
	int fd = open_dev();
	if (fd < 0)
		return 1;
	int rc = devctl(fd, ON_DCMD_SNAPSHOT, &s, sizeof(s), NULL);
	if (rc != EOK) {
		fprintf(stderr, "opticnerve: capture: %s\n", strerror(rc));
		close(fd);
		return 1;
	}
	uint8_t *buf = malloc(s.bytes ? s.bytes : 1);
	size_t got = 0;
	while (buf && got < s.bytes) {
		ssize_t r = read(fd, buf + got, s.bytes - got);
		if (r <= 0)
			break;
		got += (size_t)r;
	}
	close(fd);
	if (!buf || got != s.bytes) {
		fprintf(stderr, "opticnerve: capture: read %zu of %u bytes\n", got, s.bytes);
		free(buf);
		return 1;
	}
	FILE *f = fopen(argv[4], "wb");
	if (!f) {
		fprintf(stderr, "opticnerve: capture: %s: %s\n", argv[4], strerror(errno));
		free(buf);
		return 1;
	}
	fprintf(f, "P6\n%d %d\n255\n", s.w, s.h);
	int ok = fwrite(buf, 1, got, f) == got;
	ok = fclose(f) == 0 && ok;
	free(buf);
	if (!ok) {
		fprintf(stderr, "opticnerve: capture: writing %s failed\n", argv[4]);
		return 1;
	}
	printf("%dx%d at %d,%d, content %llu, %s\n", s.w, s.h, s.x, s.y, (unsigned long long)s.content, argv[4]);
	return 0;
}

static pid_t agent_pid(void)
{
	char buf[8192];
	if (read_status(buf, sizeof(buf)) < 0)
		return -1;
	char *p = strstr(buf, "\npid ");
	return p ? (pid_t)atol(p + 5) : -1;
}

static int cmd_stop(void)
{
	pid_t pid = agent_pid();
	if (pid <= 0)
		return 1;
	if (kill(pid, SIGTERM) != 0) {
		fprintf(stderr, "opticnerve: stop: %s\n", strerror(errno)); /* EPERM for anyone but root */
		return 1;
	}
	for (int i = 0; i < 50; i++) {
		if (access(ON_DEV_PATH, F_OK) != 0)
			return 0;
		usleep(100000);
	}
	fprintf(stderr, "opticnerve: stop: the agent is still running after 5 s\n");
	return 1;
}

static int cmd_passwd(int argc, char **argv)
{
	const char *path = "/data/opticnerve/etc/vncpasswd";
	for (int i = 0; i < argc; i++)
		if (strncmp(argv[i], "-passfile=", 10) == 0)
			path = argv[i] + 10;
	char line[64];
	if (!fgets(line, sizeof(line), stdin) || !line[0] || line[0] == '\n') {
		fprintf(stderr, "opticnerve: passwd: give the password on standard input\n");
		return 1;
	}
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0 || write(fd, line, strlen(line)) < 0 || close(fd) != 0) {
		fprintf(stderr, "opticnerve: passwd: %s: %s\n", path, strerror(errno));
		memset(line, 0, sizeof(line));
		return 1;
	}
	memset(line, 0, sizeof(line));
	if (access(ON_DEV_PATH, F_OK) != 0)
		return 0; /* no agent running: the file is written and no signal is sent */
	pid_t pid = agent_pid();
	if (pid <= 0 || kill(pid, SIGHUP) != 0) {
		fprintf(stderr, "opticnerve: passwd: could not tell the agent: %s\n", strerror(errno));
		return 1;
	}
	return 0;
}

/* pointer [on|off]: shows, or switches, the pointer arrow drawn for viewers (ON-AG-001). Any user may switch it, as
 * any user may read the status: it changes only what viewers are shown. */
static int cmd_pointer(int argc, char **argv)
{
	struct on_dev_pointer p = { -1, 0 };
	if (argc == 1 && strcmp(argv[0], "on") == 0)
		p.set = 1;
	else if (argc == 1 && strcmp(argv[0], "off") == 0)
		p.set = 0;
	else if (argc != 0) {
		fprintf(stderr, "usage: opticnerve pointer [on|off]\n");
		return 2;
	}
	limit(LIMIT_S);
	int fd = open_dev();
	if (fd < 0)
		return errno == EINTR ? not_answering() : 1;
	int rc = devctl(fd, ON_DCMD_POINTER, &p, sizeof(p), NULL);
	alarm(0);
	close(fd);
	if (rc == EINTR)
		return not_answering();
	if (rc != EOK) {
		fprintf(stderr, "opticnerve: pointer: %s\n", strerror(rc));
		return 1;
	}
	printf("pointer %s\n", p.on ? "on" : "off");
	return 0;
}

static int run(const char *cmd, int argc, char **argv)
{
	if (strcmp(cmd, "pointer") == 0)
		return cmd_pointer(argc, argv);
	if (strcmp(cmd, "status") == 0)
		return cmd_status();
	if (strcmp(cmd, "wait") == 0)
		return cmd_wait(argc, argv);
	if (strcmp(cmd, "current") == 0)
		return cmd_current();
	if (strcmp(cmd, "capture") == 0)
		return cmd_capture(argc, argv);
	if (strcmp(cmd, "stop") == 0)
		return cmd_stop();
	if (strcmp(cmd, "passwd") == 0)
		return cmd_passwd(argc, argv);
	return -1;
}

/*
 * A caller blocked on an agent that does not answer cannot always be freed by a signal or a timeout: on the Pi,
 * status stayed blocked against a stopped agent whatever the alarm did (2026-11-16). So the request runs in a child,
 * and this process waits for it at most the limit. If the limit runs out, it says so and returns; the child stays
 * blocked until the agent answers or goes.
 */
int cli_main(const char *cmd, int argc, char **argv)
{
	static const char *known[] = { "status", "wait", "current", "capture", "stop", "passwd", "pointer" };
	int ok = 0;
	for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++)
		ok |= strcmp(cmd, known[i]) == 0;
	if (!ok)
		return -1;
	long seconds = strcmp(cmd, "status") == 0 ? LIMIT_S : 2 * LIMIT_S; /* current captures first; stop waits for the agent to go */
	if (strcmp(cmd, "wait") == 0)
		seconds = opt(argc, argv, "-timeout", 2) + 1 + LIMIT_S;
	fflush(NULL);
	pid_t child = fork();
	if (child < 0)
		return run(cmd, argc, argv); /* no child: run here, with the alarm as the only limit */
	if (child == 0) {
		int rc = run(cmd, argc, argv);
		fflush(NULL); /* _exit() does not flush stdio */
		_exit(rc);
	}
	/* one second more than the child's own alarm, so that, where the alarm works, only the child reports */
	for (long waited = 0; waited < (seconds + 1) * 20; waited++) {
		int st;
		pid_t r = waitpid(child, &st, WNOHANG);
		if (r == child)
			return WIFEXITED(st) ? WEXITSTATUS(st) : 3;
		usleep(50000);
	}
	kill(child, SIGKILL);
	return not_answering();
}
