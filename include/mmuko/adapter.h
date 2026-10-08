#ifndef MMUKO_ADAPTER_H
#define MMUKO_ADAPTER_H

#include <stddef.h>
#include <stdint.h>
#include <mmuko/abi.h>

#define MMUKO_ADAPTER_VERSION 1u
#define MMUKO_ADAPTER_WIRE_SIZE 128u
#define MMUKO_ADAPTER_ONTOLOGY_SIZE 32u
#define MMUKO_ADAPTER_PAYLOAD_SIZE 76u

typedef enum mmuko_message_kind {
    MMUKO_MSG_INVALID = 0,
    MMUKO_MSG_KERNEL_READY = 1,
    MMUKO_MSG_MODULE_READY = 2,
    MMUKO_MSG_HEARTBEAT = 3,
    MMUKO_MSG_GUI_ATTACH = 4,
    MMUKO_MSG_DEGRADED = 5
} mmuko_message_kind;

typedef struct mmuko_adapter_message {
    mmuko_message_kind kind;
    mmuko_cluster_id source;
    mmuko_cluster_id target;
    uint32_t sequence;
    char ontology[MMUKO_ADAPTER_ONTOLOGY_SIZE];
    uint16_t payload_length;
    uint8_t payload[MMUKO_ADAPTER_PAYLOAD_SIZE];
} mmuko_adapter_message;

void mmuko_adapter_message_init(mmuko_adapter_message *message,
                                mmuko_message_kind kind,
                                mmuko_cluster_id source,
                                mmuko_cluster_id target,
                                uint32_t sequence,
                                const char *ontology);
int mmuko_adapter_message_set_payload(mmuko_adapter_message *message,
                                      const void *payload,
                                      size_t payload_length);
int mmuko_adapter_encode(const mmuko_adapter_message *message,
                         uint8_t wire[MMUKO_ADAPTER_WIRE_SIZE]);
int mmuko_adapter_decode(const uint8_t wire[MMUKO_ADAPTER_WIRE_SIZE],
                         mmuko_adapter_message *message);
uint32_t mmuko_adapter_crc32(const void *data, size_t length);

/* POSIX transport. The fixed wire record is the structural contract; raw C
 * structs and pointers never cross the peer boundary. */
int mmuko_adapter_send_fd(int fd, const mmuko_adapter_message *message);
int mmuko_adapter_recv_fd(int fd, mmuko_adapter_message *message, int timeout_ms);

#endif
