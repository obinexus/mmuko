#include <mmuko/adapter.h>

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <sys/socket.h>
#include <unistd.h>

static int write_full(int fd, const uint8_t *buffer, size_t length)
{
    size_t offset = 0;
    while (offset < length) {
        ssize_t n = send(fd, buffer + offset, length - offset, MSG_NOSIGNAL);
        if (n > 0) {
            offset += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        return -1;
    }
    return 0;
}

static int read_full(int fd, uint8_t *buffer, size_t length)
{
    size_t offset = 0;
    while (offset < length) {
        ssize_t n = recv(fd, buffer + offset, length - offset, 0);
        if (n > 0) {
            offset += (size_t)n;
            continue;
        }
        if (n == 0) return 1;
        if (errno == EINTR) continue;
        return -1;
    }
    return 0;
}

int mmuko_adapter_send_fd(int fd, const mmuko_adapter_message *message)
{
    uint8_t wire[MMUKO_ADAPTER_WIRE_SIZE];
    if (fd < 0) return -1;
    if (mmuko_adapter_encode(message, wire) != 0) return -1;
    return write_full(fd, wire, sizeof wire);
}

int mmuko_adapter_recv_fd(int fd, mmuko_adapter_message *message, int timeout_ms)
{
    struct pollfd pfd;
    uint8_t wire[MMUKO_ADAPTER_WIRE_SIZE];
    if (fd < 0) return -1;

    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    for (;;) {
        int rc = poll(&pfd, 1, timeout_ms);
        if (rc > 0) break;
        if (rc == 0) return 1;
        if (errno == EINTR) continue;
        return -1;
    }

    int rc = read_full(fd, wire, sizeof wire);
    if (rc != 0) return rc == 1 ? 2 : -1;
    return mmuko_adapter_decode(wire, message) == 0 ? 0 : -2;
}
