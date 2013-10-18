#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <signal.h>
#include <unistd.h>

// Exit code if an unexpected fatal error occurs.
// In general this should never happen.
#define EXIT_FATAL_ERROR 111

// pid of child process
static int s_childpid = -1;

// Handler for SIGTERM: if child process is running, send the
// signal to the child proces.
static void sigterm_handler(int signo)
{
	if (s_childpid > 0) {
		kill(s_childpid, SIGTERM);
	}
}

static void install_sigterm_handler(void)
{
	// Install handler for SIGTERM
	sigset_t mask;
	sigemptyset(mask);
	struct sigaction sa = {
		.sa_handler = &sigterm_handler,
		.sa_mask = mask,
		.sa_flags = 0,
	};
	if (sigaction(SIGTERM, &sa, NULL) != 0) {
		exit(EXIT_FATAL_ERROR);
	}
}

static void set_limit(int resource, int limit)
{
	struct rlimit rl = {
		.rlim_cur = (rlim_t) limit,
		.rlim_max = (rlim_t) limit,
	};
	if (setrlimit(resource, &rl) != 0) {
		exit(EXIT_FATAL_ERROR);
	}
}

static void set_resource_limits(void)
{
	char *limits = getenv("CC_PROCESS_RESOURCE_LIMITS");
	if (limits != NULL) {
		char *save;
		limits = strdup(limits); // make a copy
		char *limit = strtok_r(limits, " ", &save);
		while (limit != NULL) {
			if (*limit++ != '-') {
				continue;
			}
			char type = *limit++;
			int value = atoi(limit);

			switch (type) {
			case 'f': // File size limit in KB
				set_limit(RLIMIT_FSIZE, value*1024);
				break;

			case 's': // Maximum stack size in KB
				set_limit(RLIMIT_STACK, value*1024);
				break;

			case 't': // Maximum CPU time in seconds
				set_limit(RLIMIT_CPU, value);
				break;

			case 'u': // Maximum number of processes
				set_limit(RLIMIT_NPROC, value);
				break;

			case 'v': // Maximum virtual memory in KB
				set_limit(RLIMIT_AS, value);
				break;
			}

			limit = strtok_r(NULL, " ", &save);
		}
	}
}

static char *make_env_entry(const char *a, const char *b)
{
	const size_t alen = strlen(a), blen = strlen(b);
	const size_t tot = alen + 1 + blen; // space for name, equals, value
	char *result = malloc(tot + 1); // leave room for nul terminator
	strcpy(result, a);
	result[alen] = '=';
	strcpy(result + alen + 1, b);
	result[tot] = '\0';
	return result;
}

static char **create_env(char **env)
{
	int n = 0;
	while (env[n] != NULL) {
		n++;
	}
	char **new_env = malloc((n+3) * sizeof(char*));

	// Copy current environment variables
	for (int i = 0; i < n; i++) {
		new_env[i] = env[i];
	}

	// Set LD_PRELOAD if requested
	if (getenv("CC_LD_PRELOAD") != NULL) {
		new_env[n++] = make_env_entry("LD_PRELOAD", getenv("CC_LD_PRELOAD"));
	}
	// Set EASYSANDBOX_HEAPSIZE if requested
	if (getenv("CC_EASYSANDBOX_HEAPSIZE") != NULL) {
		new_env[n++] = make_env_entry("EASYSANDBOX_HEAPSIZE", getenv("CC_EASYSANDBOX_HEAPSIZE"));
	}

	// Terminate the list
	new_env[n] = NULL;

	return new_env;
}

int main(int argc, char **argv, char **env)
{
	install_sigterm_handler();

	// Create a pipe so that the parent process can let the child know
	// that it has closed its stdin/stdout/stderr files (which are
	// actually pipes connected to the parent builder process.)
	// We don't actually execute the "real" subprocess until this has
	// happened, because we have observed some strange issues if
	// both the real child subprocess and the wrapper process have
	// the stdin pipe open at the same time.
	int pipe[2];
	// TODO

	// Fork the child process
	s_childpid = fork();
	if (s_childpid == -1) {
		// Fork failed!
		exit(EXIT_FATAL_ERROR);
	}
	if (s_childpid != 0) {
		// in the child

		// Wait for parent's signal that it is ok to proceed
		// TODO

		// Set resource limits as specified by CC_PROCESS_RESOURCE_LIMITS
		set_resource_limits();

		// Create environment, setting up sandboxing if requested
		env = create_env(env);

		// Get the executable and arguments
		char *exe = argv[1];
		char **args = argv + 2;

		// Exec the child process!
		int rc = execve(exe, args, env);

		// An error occurred executing the child process.
		exit(127);
	} else {
		// in the parent

		// Close stdin, stdout, and stderr
		close(0);
		close(1);
		close(2);
		
		// TODO: tell the child it's safe to execute the program

		// TODO: wait for child to exit

		// TODO: create exit status file if requested
	}
}