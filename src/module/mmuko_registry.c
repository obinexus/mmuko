#include <mmuko/module.h>

#include <string.h>

extern const mmuko_graphics_module mmuko_orion_module;

const mmuko_graphics_module *mmuko_module_resolve_graphics(const char *name)
{
    static const mmuko_graphics_module *const modules[] = {&mmuko_orion_module};
    if (!name) return NULL;
    for (unsigned i = 0; i < sizeof modules / sizeof modules[0]; i++) {
        if (strcmp(modules[i]->name, name) == 0 && modules[i]->abi_version == MMUKO_ABI_VERSION)
            return modules[i];
    }
    return NULL;
}
