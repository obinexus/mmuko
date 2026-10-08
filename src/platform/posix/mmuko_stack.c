#define _DEFAULT_SOURCE
#include <mmuko/stack.h>

#include <sys/mman.h>
#include <unistd.h>

int mmuko_stack_allocate(mmuko_stack_region *region, size_t size)
{
    long page = sysconf(_SC_PAGESIZE);
    size_t page_size, usable, total;
    void *mapping;

    if (!region || size == 0u || page <= 0) return -1;
    page_size = (size_t)page;
    usable = (size + page_size - 1u) / page_size * page_size;
    total = usable + page_size;

    mapping = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) return -1;
    /* Stacks grow down: the lowest page is the guard. */
    if (mprotect(mapping, page_size, PROT_NONE) != 0) {
        munmap(mapping, total);
        return -1;
    }

    region->mapping = mapping;
    region->mapping_size = total;
    region->base = (unsigned char *)mapping + page_size;
    region->size = usable;
    return 0;
}

void mmuko_stack_release(mmuko_stack_region *region)
{
    if (!region || !region->mapping) return;
    munmap(region->mapping, region->mapping_size);
    region->mapping = NULL;
    region->mapping_size = 0;
    region->base = NULL;
    region->size = 0;
}
