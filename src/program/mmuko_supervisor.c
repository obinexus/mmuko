#define _POSIX_C_SOURCE 200809L
#include "mmuko_supervisor.h"

#include <mmuko/topology.h>

#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int write_all(int fd, const void *buffer, size_t length)
{
    const unsigned char *cursor = (const unsigned char *)buffer;
    size_t written = 0;

    while (written < length) {
        ssize_t n = write(fd, cursor + written, length - written);
        if (n > 0) {
            written += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n == 0) errno = EIO;
        return -1;
    }
    return 0;
}

static int wait_status_code(pid_t pid)
{
    int status = 0;
    for (;;) {
        if (waitpid(pid, &status, 0) >= 0) break;
        if (errno == EINTR) continue;
        return 127;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 127;
}

static int read_ready_tokens(int fd)
{
    unsigned char tokens[2] = {0, 0};
    size_t offset = 0;
    while (offset < sizeof tokens) {
        ssize_t n = read(fd, tokens + offset, sizeof tokens - offset);
        if (n > 0) {
            offset += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        return -1;
    }
    int have_kernel = 0, have_module = 0;
    for (size_t i = 0; i < sizeof tokens; i++) {
        have_kernel |= tokens[i] == MMUKO_CLUSTER_A_KERNEL;
        have_module |= tokens[i] == MMUKO_CLUSTER_B_MODULE;
    }
    return (have_kernel && have_module) ? 0 : -1;
}

int mmuko_supervisor_signal_ready(int ready_fd, mmuko_cluster_id cluster)
{
    const unsigned char token = (unsigned char)cluster;
    if (ready_fd < 0) return 0;
    return write_all(ready_fd, &token, sizeof token);
}

void mmuko_supervisor_close_ready(int ready_fd)
{
    if (ready_fd >= 0) close(ready_fd);
}

_Noreturn static void run_supervisor(const mmuko_supervisor_spec *spec, int ready_fd)
{
    uint32_t restarts = 0;
    int code = 0;

    printf("[program] supervisor node=1 parent=0 depth=1 color=%d pid=%ld (process isolation layer)\n",
           (int)mmuko_topology_color_for_depth(1), (long)getpid());

    for (uint32_t generation = 0;; generation++) {
        pid_t domain = fork();
        if (domain < 0) {
            perror("fork execution domain");
            _exit(70);
        }
        if (domain == 0) {
            int rc = spec->domain(generation, generation == 0u ? ready_fd : -1);
            _exit(rc);
        }
        if (generation == 0u) {
            mmuko_supervisor_close_ready(ready_fd);
            ready_fd = -1;
        }

        code = wait_status_code(domain);
        if (code == 0) {
            printf("[program] supervisor: execution domain generation=%u completed\n", generation);
            break;
        }
        if (restarts >= spec->restart_budget) {
            printf("[program] supervisor: execution domain generation=%u failed code=%d; restart budget exhausted\n",
                   generation, code);
            break;
        }
        restarts++;
        printf("[program] supervisor: execution domain generation=%u failed code=%d; restarting process (restart %u/%u)\n",
               generation, code, restarts, spec->restart_budget);
    }

    printf("[program] supervisor exiting code=%d\n", code);
    _exit(code);
}

int mmuko_supervisor_launch(const mmuko_supervisor_spec *spec)
{
    int ready[2] = {-1, -1};
    pid_t supervisor;

    if (!spec || !spec->domain) return 64;
    if (pipe(ready) != 0) {
        perror("pipe");
        return 71;
    }

    supervisor = fork();
    if (supervisor < 0) {
        perror("fork supervisor");
        close(ready[0]);
        close(ready[1]);
        return 72;
    }
    if (supervisor == 0) {
        close(ready[0]);
        run_supervisor(spec, ready[1]);
    }

    close(ready[1]);
    if (read_ready_tokens(ready[0]) != 0) {
        fputs("[program] execution domain hand-off failed\n", stderr);
        close(ready[0]);
        return 74;
    }
    close(ready[0]);
    return 0;
}
