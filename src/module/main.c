#include <mmuko/adapter.h>
#include <mmuko/channel.h>
#include <mmuko/context.h>
#include <mmuko/module.h>

#include <stdio.h>
#include <string.h>

#define MMUKO_MODULE_ITERATIONS 3u

static unsigned module_drain_peer(const mmuko_entry_context *context)
{
    mmuko_adapter_message incoming;
    unsigned received = 0;

    if (!context->peer) return 0;
    for (;;) {
        int rc = mmuko_channel_try_recv(context->peer, &incoming);
        if (rc == MMUKO_CHANNEL_OK) {
            printf("[module] peer message kind=%d ontology=%s\n", (int)incoming.kind, incoming.ontology);
            received++;
            continue;
        }
        if (rc < 0) printf("[module] rejected peer record status=%d\n", rc);
        return received;
    }
}

int mmuko_module_main(const mmuko_entry_context *context)
{
    const mmuko_graphics_module *graphics;
    unsigned received = 0;
    unsigned yields = 0;
    uint32_t frames = 0;
    int rc;

    if (!context || context->abi_version != MMUKO_ABI_VERSION ||
        context->cluster_id != MMUKO_CLUSTER_B_MODULE) return 64;

    graphics = mmuko_module_resolve_graphics("orion");
    if (!graphics) return 65;

    printf("[module] mmuko_module_main entered generation=%u resolved=%s (GUI concern)\n",
           context->generation, graphics->name);

    if (context->peer) {
        mmuko_adapter_message outgoing;
        const char payload[] = "gui=orion";
        mmuko_adapter_message_init(&outgoing, MMUKO_MSG_MODULE_READY,
                                   MMUKO_CLUSTER_B_MODULE, MMUKO_CLUSTER_A_KERNEL,
                                   context->generation + 1u, graphics->ontology);
        if (mmuko_adapter_message_set_payload(&outgoing, payload, strlen(payload)) != 0) return 66;
        if (mmuko_channel_send(context->peer, &outgoing) != MMUKO_CHANNEL_OK)
            printf("[module] peer channel unavailable; continuing degraded\n");
    }

    rc = graphics->start(context);
    if (rc != 0) return rc;

    for (uint32_t iteration = 0; iteration < MMUKO_MODULE_ITERATIONS; iteration++) {
        if (iteration != 0u && context->self) {
            printf("[module] yield\n");
            if (mmuko_context_yield(context->self) != MMUKO_CONTEXT_OK) return 67;
            yields++;
            printf("[module] resumed\n");
        }
        printf("[module] Orion/GUI iteration=%u\n", iteration);
        received += module_drain_peer(context);
        if (graphics->render(context, iteration) != 0) return 68;
        frames++;
    }

    /* graphics and frames live on this context's own stack across yields. */
    if (frames != MMUKO_MODULE_ITERATIONS) return 69;
    printf("[module] state preserved across %u yields frames=%u surface=%s\n", yields, frames, graphics->name);
    if (context->peer && received == 0u)
        printf("[module] peer unavailable; module cluster remains operational (degraded)\n");
    return 0;
}
