#include <mmuko/adapter.h>
#include <mmuko/channel.h>
#include <mmuko/context.h>
#include <mmuko/kernel.h>

#include <stdio.h>

#define MMUKO_KERNEL_ITERATIONS 3u

static unsigned kernel_drain_peer(const mmuko_entry_context *context)
{
    mmuko_adapter_message incoming;
    unsigned received = 0;

    if (!context->peer) return 0;
    for (;;) {
        int rc = mmuko_channel_try_recv(context->peer, &incoming);
        if (rc == MMUKO_CHANNEL_OK) {
            printf("[kernel] peer message kind=%d ontology=%s\n", (int)incoming.kind, incoming.ontology);
            received++;
            continue;
        }
        if (rc < 0) printf("[kernel] rejected peer record status=%d\n", rc);
        return received;
    }
}

int mmuko_kernel_main(const mmuko_entry_context *context)
{
    mmuko_byte memory[MMUKO_MEMORY_SIZE];
    mmuko_system system;
    uint32_t checksum;
    unsigned received = 0;
    unsigned yields = 0;

    if (!context || context->abi_version != MMUKO_ABI_VERSION ||
        context->cluster_id != MMUKO_CLUSTER_A_KERNEL) return 64;

    printf("[kernel] mmuko_kernel_main entered generation=%u (TUI concern)\n", context->generation);

    mmuko_system_init(&system, memory, MMUKO_MEMORY_SIZE);
    mmuko_boot_status status = mmuko_boot(&system);
    if (status != MMUKO_BOOT_OK) {
        fprintf(stderr, "[kernel] boot failed status=%d\n", (int)status);
        return 65;
    }
    checksum = mmuko_system_checksum(&system);
    printf("[kernel] boot frame=%s checksum=0x%08x\n",
           mmuko_direction_name(system.frame_of_reference), checksum);

    if (context->peer) {
        mmuko_adapter_message outgoing;
        char payload[32];
        int n = snprintf(payload, sizeof payload, "checksum=%08x", checksum);
        if (n < 0 || (size_t)n >= sizeof payload) return 66;
        mmuko_adapter_message_init(&outgoing, MMUKO_MSG_KERNEL_READY,
                                   MMUKO_CLUSTER_A_KERNEL, MMUKO_CLUSTER_B_MODULE,
                                   context->generation + 1u, "mmuko.kernel.boot");
        if (mmuko_adapter_message_set_payload(&outgoing, payload, (size_t)n) != 0) return 66;
        if (mmuko_channel_send(context->peer, &outgoing) != MMUKO_CHANNEL_OK)
            printf("[kernel] peer channel unavailable; continuing degraded\n");
    }

    for (uint32_t iteration = 0; iteration < MMUKO_KERNEL_ITERATIONS; iteration++) {
        if (iteration != 0u && context->self) {
            printf("[kernel] yield\n");
            if (mmuko_context_yield(context->self) != MMUKO_CONTEXT_OK) return 67;
            yields++;
            printf("[kernel] resumed\n");
        }
        printf("[kernel] iteration=%u\n", iteration);
        received += kernel_drain_peer(context);
    }

    /* memory[] and system live on this context's own stack. Had any switch
     * lost or clobbered them, the recomputed checksum would differ. */
    if (!system.boot_complete || mmuko_system_checksum(&system) != checksum) {
        printf("[kernel] boot state lost across yields\n");
        return 68;
    }
    printf("[kernel] state preserved across %u yields checksum=0x%08x\n", yields, checksum);
    if (context->peer && received == 0u)
        printf("[kernel] peer unavailable; kernel cluster remains operational (degraded)\n");
    return 0;
}
