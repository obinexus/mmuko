#include <mmuko/channel.h>

_Static_assert((MMUKO_CHANNEL_DEPTH & (MMUKO_CHANNEL_DEPTH - 1u)) == 0u,
               "channel depth must be a power of two so index wrap-around stays consistent");

static void ring_init(mmuko_channel_ring *ring)
{
    ring->read_index = 0;
    ring->write_index = 0;
}

static uint32_t ring_count(const mmuko_channel_ring *ring)
{
    return ring->write_index - ring->read_index;
}

static void endpoint_init(mmuko_channel_endpoint *endpoint,
                          mmuko_cluster_id cluster,
                          mmuko_channel_ring *inbound,
                          mmuko_channel_ring *outbound,
                          const mmuko_channel_endpoint *peer)
{
    endpoint->cluster = cluster;
    endpoint->inbound = inbound;
    endpoint->outbound = outbound;
    endpoint->peer = peer;
    endpoint->open = true;
}

void mmuko_channel_init(mmuko_channel *channel, mmuko_cluster_id a, mmuko_cluster_id b)
{
    if (!channel) return;
    ring_init(&channel->a_to_b);
    ring_init(&channel->b_to_a);
    endpoint_init(&channel->a, a, &channel->b_to_a, &channel->a_to_b, &channel->b);
    endpoint_init(&channel->b, b, &channel->a_to_b, &channel->b_to_a, &channel->a);
}

mmuko_channel_endpoint *mmuko_channel_endpoint_of(mmuko_channel *channel, mmuko_cluster_id cluster)
{
    if (!channel) return 0;
    if (channel->a.cluster == cluster) return &channel->a;
    if (channel->b.cluster == cluster) return &channel->b;
    return 0;
}

void mmuko_channel_close(mmuko_channel_endpoint *endpoint)
{
    if (endpoint) endpoint->open = false;
}

int mmuko_channel_send(mmuko_channel_endpoint *endpoint, const mmuko_adapter_message *message)
{
    mmuko_channel_ring *ring;

    if (!endpoint || !message) return MMUKO_CHANNEL_EINVAL;
    if (!endpoint->open || !endpoint->peer->open) return MMUKO_CHANNEL_CLOSED;
    if (message->source != endpoint->cluster) return MMUKO_CHANNEL_EIDENTITY;

    ring = endpoint->outbound;
    if (ring_count(ring) >= MMUKO_CHANNEL_DEPTH) return MMUKO_CHANNEL_FULL;
    if (mmuko_adapter_encode(message, ring->records[ring->write_index % MMUKO_CHANNEL_DEPTH]) != 0)
        return MMUKO_CHANNEL_EINVAL;
    ring->write_index++;
    return MMUKO_CHANNEL_OK;
}

int mmuko_channel_try_recv(mmuko_channel_endpoint *endpoint, mmuko_adapter_message *message)
{
    mmuko_channel_ring *ring;
    const uint8_t *record;

    if (!endpoint || !message) return MMUKO_CHANNEL_EINVAL;
    if (!endpoint->open) return MMUKO_CHANNEL_CLOSED;

    ring = endpoint->inbound;
    if (ring_count(ring) == 0u) return endpoint->peer->open ? MMUKO_CHANNEL_EMPTY : MMUKO_CHANNEL_CLOSED;

    record = ring->records[ring->read_index % MMUKO_CHANNEL_DEPTH];
    ring->read_index++;
    if (mmuko_adapter_decode(record, message) != 0) return MMUKO_CHANNEL_EDECODE;
    if (message->target != endpoint->cluster) return MMUKO_CHANNEL_EMISROUTED;
    return MMUKO_CHANNEL_OK;
}
