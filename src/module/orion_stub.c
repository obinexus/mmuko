#include <mmuko/module.h>

#include <stdio.h>

static int orion_start(const mmuko_entry_context *context)
{
    printf("[module/orion] graphical module plugged in generation=%u\n", context->generation);
    return 0;
}

static int orion_render(const mmuko_entry_context *context, uint32_t frame)
{
    printf("[module/orion] frame=%u composed generation=%u\n", frame, context->generation);
    return 0;
}

const mmuko_graphics_module mmuko_orion_module = {
    .abi_version = MMUKO_ABI_VERSION,
    .name = "orion",
    .ontology = "mmuko.module.gui.orion",
    .start = orion_start,
    .render = orion_render
};
