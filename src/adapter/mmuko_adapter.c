#include <mmuko/adapter.h>

/* Freestanding codec: no libc. Local byte helpers replace <string.h>. */

#define OFF_MAGIC       0u
#define OFF_VERSION     4u
#define OFF_KIND        6u
#define OFF_SOURCE      8u
#define OFF_TARGET      9u
#define OFF_PAYLOAD_LEN 10u
#define OFF_SEQUENCE    12u
#define OFF_ONTOLOGY    16u
#define OFF_PAYLOAD     48u
#define OFF_CRC         124u

static void bytes_zero(void *destination, size_t length)
{
    uint8_t *d = (uint8_t *)destination;
    for (size_t i = 0; i < length; i++) d[i] = 0;
}

static void bytes_copy(void *destination, const void *source, size_t length)
{
    uint8_t *d = (uint8_t *)destination;
    const uint8_t *s = (const uint8_t *)source;
    for (size_t i = 0; i < length; i++) d[i] = s[i];
}

static int bytes_equal(const void *left, const void *right, size_t length)
{
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    for (size_t i = 0; i < length; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static void put_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static size_t bounded_strlen(const char *s, size_t n)
{
    size_t i = 0;
    if (!s) return 0;
    while (i < n && s[i] != '\0') i++;
    return i;
}

uint32_t mmuko_adapter_crc32(const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < length; i++) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; bit++) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

void mmuko_adapter_message_init(mmuko_adapter_message *message,
                                mmuko_message_kind kind,
                                mmuko_cluster_id source,
                                mmuko_cluster_id target,
                                uint32_t sequence,
                                const char *ontology)
{
    bytes_zero(message, sizeof *message);
    message->kind = kind;
    message->source = source;
    message->target = target;
    message->sequence = sequence;
    if (ontology) {
        size_t n = bounded_strlen(ontology, MMUKO_ADAPTER_ONTOLOGY_SIZE - 1u);
        bytes_copy(message->ontology, ontology, n);
        message->ontology[n] = '\0';
    }
}

int mmuko_adapter_message_set_payload(mmuko_adapter_message *message,
                                      const void *payload,
                                      size_t payload_length)
{
    if (payload_length > MMUKO_ADAPTER_PAYLOAD_SIZE) return -1;
    bytes_zero(message->payload, sizeof message->payload);
    if (payload_length != 0u && payload != NULL) {
        bytes_copy(message->payload, payload, payload_length);
    }
    message->payload_length = (uint16_t)payload_length;
    return 0;
}

int mmuko_adapter_encode(const mmuko_adapter_message *message,
                         uint8_t wire[MMUKO_ADAPTER_WIRE_SIZE])
{
    if (!message || !wire) return -1;
    if (message->payload_length > MMUKO_ADAPTER_PAYLOAD_SIZE) return -1;
    if (message->source < MMUKO_CLUSTER_PROGRAM || message->source > MMUKO_CLUSTER_B_MODULE) return -1;
    if (message->target < MMUKO_CLUSTER_PROGRAM || message->target > MMUKO_CLUSTER_B_MODULE) return -1;

    bytes_zero(wire, MMUKO_ADAPTER_WIRE_SIZE);
    bytes_copy(wire + OFF_MAGIC, "MMKA", 4);
    put_u16(wire + OFF_VERSION, MMUKO_ADAPTER_VERSION);
    put_u16(wire + OFF_KIND, (uint16_t)message->kind);
    wire[OFF_SOURCE] = (uint8_t)message->source;
    wire[OFF_TARGET] = (uint8_t)message->target;
    put_u16(wire + OFF_PAYLOAD_LEN, message->payload_length);
    put_u32(wire + OFF_SEQUENCE, message->sequence);
    bytes_copy(wire + OFF_ONTOLOGY, message->ontology, MMUKO_ADAPTER_ONTOLOGY_SIZE);
    bytes_copy(wire + OFF_PAYLOAD, message->payload, message->payload_length);
    put_u32(wire + OFF_CRC, mmuko_adapter_crc32(wire, OFF_CRC));
    return 0;
}

int mmuko_adapter_decode(const uint8_t wire[MMUKO_ADAPTER_WIRE_SIZE],
                         mmuko_adapter_message *message)
{
    if (!wire || !message) return -1;
    if (!bytes_equal(wire + OFF_MAGIC, "MMKA", 4)) return -2;
    if (get_u16(wire + OFF_VERSION) != MMUKO_ADAPTER_VERSION) return -3;
    if (get_u32(wire + OFF_CRC) != mmuko_adapter_crc32(wire, OFF_CRC)) return -4;

    uint16_t payload_length = get_u16(wire + OFF_PAYLOAD_LEN);
    if (payload_length > MMUKO_ADAPTER_PAYLOAD_SIZE) return -5;
    if (wire[OFF_SOURCE] > MMUKO_CLUSTER_B_MODULE || wire[OFF_TARGET] > MMUKO_CLUSTER_B_MODULE) return -6;

    bytes_zero(message, sizeof *message);
    message->kind = (mmuko_message_kind)get_u16(wire + OFF_KIND);
    message->source = (mmuko_cluster_id)wire[OFF_SOURCE];
    message->target = (mmuko_cluster_id)wire[OFF_TARGET];
    message->sequence = get_u32(wire + OFF_SEQUENCE);
    message->payload_length = payload_length;
    bytes_copy(message->ontology, wire + OFF_ONTOLOGY, MMUKO_ADAPTER_ONTOLOGY_SIZE);
    message->ontology[MMUKO_ADAPTER_ONTOLOGY_SIZE - 1u] = '\0';
    bytes_copy(message->payload, wire + OFF_PAYLOAD, payload_length);
    return 0;
}
