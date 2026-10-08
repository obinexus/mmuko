#ifndef MMUKO_CHANNEL_H
#define MMUKO_CHANNEL_H

/* In-domain data plane: a full-duplex channel of fixed adapter wire records.
 *
 * Only encoded MMKA records (mmuko_adapter_encode) are stored in the rings;
 * no C struct, pointer, or subsystem object crosses between the kernel and
 * module contexts. Non-blocking by design: a context that finds nothing to
 * read yields instead of waiting. Freestanding (no libc). */

#include <stdbool.h>
#include <stdint.h>
#include <mmuko/abi.h>
#include <mmuko/adapter.h>

#define MMUKO_CHANNEL_DEPTH 8u

typedef enum mmuko_channel_status {
    MMUKO_CHANNEL_OK = 0,
    MMUKO_CHANNEL_EMPTY = 1,      /* nothing to read yet */
    MMUKO_CHANNEL_FULL = 2,       /* peer has not drained its inbound ring */
    MMUKO_CHANNEL_CLOSED = 3,     /* this endpoint or its peer is closed */
    MMUKO_CHANNEL_EINVAL = -1,
    MMUKO_CHANNEL_EIDENTITY = -2, /* message source is not this endpoint's cluster */
    MMUKO_CHANNEL_EDECODE = -3,   /* record failed magic/version/CRC/bounds checks */
    MMUKO_CHANNEL_EMISROUTED = -4 /* record targets a different cluster */
} mmuko_channel_status;

typedef struct mmuko_channel_ring {
    uint8_t records[MMUKO_CHANNEL_DEPTH][MMUKO_ADAPTER_WIRE_SIZE];
    uint32_t read_index;
    uint32_t write_index;
} mmuko_channel_ring;

typedef struct mmuko_channel_endpoint {
    mmuko_cluster_id cluster;
    mmuko_channel_ring *inbound;
    mmuko_channel_ring *outbound;
    const struct mmuko_channel_endpoint *peer;
    bool open;
} mmuko_channel_endpoint;

typedef struct mmuko_channel {
    mmuko_channel_ring a_to_b;
    mmuko_channel_ring b_to_a;
    mmuko_channel_endpoint a;
    mmuko_channel_endpoint b;
} mmuko_channel;

void mmuko_channel_init(mmuko_channel *channel, mmuko_cluster_id a, mmuko_cluster_id b);
mmuko_channel_endpoint *mmuko_channel_endpoint_of(mmuko_channel *channel, mmuko_cluster_id cluster);
void mmuko_channel_close(mmuko_channel_endpoint *endpoint);

/* Encode and enqueue one record. The message source must be the endpoint's
 * own cluster: an endpoint can only speak for itself. */
int mmuko_channel_send(mmuko_channel_endpoint *endpoint, const mmuko_adapter_message *message);

/* Dequeue and decode one record without blocking. A malformed record is
 * consumed and reported, so it cannot wedge the channel. */
int mmuko_channel_try_recv(mmuko_channel_endpoint *endpoint, mmuko_adapter_message *message);

#endif
