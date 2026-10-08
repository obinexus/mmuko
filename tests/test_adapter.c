#define _POSIX_C_SOURCE 200809L
#include <mmuko/adapter.h>
#include <mmuko/channel.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void test_codec(void)
{
    mmuko_adapter_message in, out;
    uint8_t wire[MMUKO_ADAPTER_WIRE_SIZE];
    const char payload[] = "frame=southwest";

    mmuko_adapter_message_init(&in, MMUKO_MSG_KERNEL_READY,
                               MMUKO_CLUSTER_A_KERNEL, MMUKO_CLUSTER_B_MODULE,
                               42u, "mmuko.kernel.boot");
    assert(mmuko_adapter_message_set_payload(&in, payload, strlen(payload)) == 0);
    assert(mmuko_adapter_encode(&in, wire) == 0);
    assert(memcmp(wire, "MMKA", 4) == 0);
    assert(mmuko_adapter_decode(wire, &out) == 0);
    assert(out.kind == in.kind);
    assert(out.source == in.source);
    assert(out.target == in.target);
    assert(out.sequence == 42u);
    assert(strcmp(out.ontology, "mmuko.kernel.boot") == 0);
    assert(out.payload_length == strlen(payload));
    assert(memcmp(out.payload, payload, out.payload_length) == 0);

    wire[64] ^= 0x01u;
    assert(mmuko_adapter_decode(wire, &out) != 0);
}

static void test_channel(void)
{
    mmuko_channel channel;
    mmuko_channel_endpoint *kernel, *module;
    mmuko_adapter_message message, received;

    mmuko_channel_init(&channel, MMUKO_CLUSTER_A_KERNEL, MMUKO_CLUSTER_B_MODULE);
    kernel = mmuko_channel_endpoint_of(&channel, MMUKO_CLUSTER_A_KERNEL);
    module = mmuko_channel_endpoint_of(&channel, MMUKO_CLUSTER_B_MODULE);
    assert(kernel && module && kernel != module);

    assert(mmuko_channel_try_recv(module, &received) == MMUKO_CHANNEL_EMPTY);

    mmuko_adapter_message_init(&message, MMUKO_MSG_KERNEL_READY,
                               MMUKO_CLUSTER_A_KERNEL, MMUKO_CLUSTER_B_MODULE, 1u, "mmuko.kernel.boot");
    assert(mmuko_channel_send(kernel, &message) == MMUKO_CHANNEL_OK);
    /* Only the encoded record crosses: the ring holds MMKA bytes. */
    assert(memcmp(channel.a_to_b.records[0], "MMKA", 4) == 0);
    assert(mmuko_channel_try_recv(module, &received) == MMUKO_CHANNEL_OK);
    assert(received.kind == MMUKO_MSG_KERNEL_READY && received.sequence == 1u);
    assert(strcmp(received.ontology, "mmuko.kernel.boot") == 0);

    /* An endpoint cannot speak for the other cluster. */
    assert(mmuko_channel_send(module, &message) == MMUKO_CHANNEL_EIDENTITY);

    /* Bounded ring. */
    for (unsigned i = 0; i < MMUKO_CHANNEL_DEPTH; i++) assert(mmuko_channel_send(kernel, &message) == MMUKO_CHANNEL_OK);
    assert(mmuko_channel_send(kernel, &message) == MMUKO_CHANNEL_FULL);
    for (unsigned i = 0; i < MMUKO_CHANNEL_DEPTH; i++) assert(mmuko_channel_try_recv(module, &received) == MMUKO_CHANNEL_OK);

    /* A corrupted record is consumed and reported, not delivered. */
    assert(mmuko_channel_send(kernel, &message) == MMUKO_CHANNEL_OK);
    channel.a_to_b.records[(channel.a_to_b.write_index - 1u) % MMUKO_CHANNEL_DEPTH][70] ^= 0x80u;
    assert(mmuko_channel_try_recv(module, &received) == MMUKO_CHANNEL_EDECODE);
    assert(mmuko_channel_try_recv(module, &received) == MMUKO_CHANNEL_EMPTY);

    /* Peer closed: drained channel reports CLOSED. */
    mmuko_channel_close(kernel);
    assert(mmuko_channel_try_recv(module, &received) == MMUKO_CHANNEL_CLOSED);
    assert(mmuko_channel_send(kernel, &message) == MMUKO_CHANNEL_CLOSED);
}

/* The same wire contract over a POSIX socket (process-boundary binding). */
static void test_fd_transport(void)
{
    int fds[2];
    mmuko_adapter_message message, received;

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    mmuko_adapter_message_init(&message, MMUKO_MSG_HEARTBEAT,
                               MMUKO_CLUSTER_B_MODULE, MMUKO_CLUSTER_A_KERNEL, 7u, "mmuko.module.gui.orion");
    assert(mmuko_adapter_send_fd(fds[0], &message) == 0);
    assert(mmuko_adapter_recv_fd(fds[1], &received, 1000) == 0);
    assert(received.kind == MMUKO_MSG_HEARTBEAT && received.sequence == 7u);
    assert(mmuko_adapter_recv_fd(fds[1], &received, 10) == 1); /* timeout */
    close(fds[0]);
    close(fds[1]);
}

int main(void)
{
    test_codec();
    test_channel();
    test_fd_transport();
    puts("adapter (codec, in-domain channel, fd transport): HELD");
    return 0;
}
