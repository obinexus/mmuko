#include <mmuko/system.h>

/* Freestanding: the boot phases use no libc. */

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

typedef struct superposition_entry {
    int base;
    mmuko_direction primary;
    mmuko_direction secondary;
} superposition_entry;

static const uint16_t spin_mrad_values[8] = {785, 1047, 1571, 3142, 6283, 1571, 1047, 785};
static const mmuko_direction direction_values[8] = {
    MMUKO_DIR_N, MMUKO_DIR_NE, MMUKO_DIR_E, MMUKO_DIR_SE,
    MMUKO_DIR_S, MMUKO_DIR_SW, MMUKO_DIR_W, MMUKO_DIR_NW
};
static const int entangled_pairs[8] = {7, 6, 5, -1, -1, 2, 1, 0};
static const superposition_entry superposition_table[] = {
    {12, MMUKO_DIR_S,  MMUKO_DIR_N},
    {10, MMUKO_DIR_SE, MMUKO_DIR_N},
    {8,  MMUKO_DIR_E,  MMUKO_DIR_W},
    {6,  MMUKO_DIR_SW, MMUKO_DIR_E},
    {4,  MMUKO_DIR_W,  MMUKO_DIR_E},
    {2,  MMUKO_DIR_NE, MMUKO_DIR_W},
    {1,  MMUKO_DIR_N,  MMUKO_DIR_S}
};

static int iabs_int(int value) { return value < 0 ? -value : value; }

const char *mmuko_direction_name(mmuko_direction direction)
{
    static const char *const names[] = {
        "NORTH", "NORTHEAST", "EAST", "SOUTHEAST", "SOUTH",
        "SOUTHWEST", "WEST", "NORTHWEST", "UNDEFINED"
    };
    if ((unsigned)direction >= ARRAY_LEN(names)) return "INVALID";
    return names[direction];
}

static mmuko_state resolve_state(int index, uint8_t value)
{
    uint8_t bit = (value >> index) & 1u;
    uint8_t neighbor = (value >> ((index + 1) & 7)) & 1u;
    if (bit && neighbor) return MMUKO_STATE_UP;
    if (bit) return MMUKO_STATE_CHARM;
    if (neighbor) return MMUKO_STATE_STRANGE;
    return MMUKO_STATE_DOWN;
}

static mmuko_state flip_state(mmuko_state state)
{
    switch (state) {
    case MMUKO_STATE_UP: return MMUKO_STATE_DOWN;
    case MMUKO_STATE_DOWN: return MMUKO_STATE_UP;
    case MMUKO_STATE_CHARM: return MMUKO_STATE_STRANGE;
    case MMUKO_STATE_STRANGE: return MMUKO_STATE_CHARM;
    case MMUKO_STATE_LEFT: return MMUKO_STATE_RIGHT;
    case MMUKO_STATE_RIGHT: return MMUKO_STATE_LEFT;
    default: return state;
    }
}

static int nearest_base(int base)
{
    static const int valid[] = {12, 10, 8, 6, 4, 2, 1};
    int result = valid[0];
    int distance = iabs_int(base - result);
    for (size_t i = 1; i < ARRAY_LEN(valid); i++) {
        int d = iabs_int(base - valid[i]);
        if (d < distance) {
            result = valid[i];
            distance = d;
        }
    }
    return result;
}

static void lookup_superposition(int base, mmuko_direction *primary, mmuko_direction *secondary)
{
    int key = base;
    for (unsigned pass = 0; pass < 2; pass++) {
        for (size_t i = 0; i < ARRAY_LEN(superposition_table); i++) {
            if (superposition_table[i].base == key) {
                *primary = superposition_table[i].primary;
                *secondary = superposition_table[i].secondary;
                return;
            }
        }
        key = nearest_base(base);
    }
    *primary = MMUKO_DIR_N;
    *secondary = MMUKO_DIR_S;
}

static void init_cubit_ring(mmuko_byte *byte)
{
    for (int i = 0; i < 8; i++) {
        mmuko_cubit *cubit = &byte->cubit_ring[i];
        cubit->index = i;
        cubit->value = (byte->raw_value >> i) & 1u;
        cubit->spin_mrad = spin_mrad_values[i];
        cubit->direction = direction_values[i];
        cubit->state = resolve_state(i, byte->raw_value);
        cubit->entangled_with = entangled_pairs[i];
        cubit->superposed = cubit->entangled_with >= 0;
    }
}

static mmuko_boot_status phase1(mmuko_system *system)
{
    for (size_t i = 0; i < system->memory_size; i++) {
        mmuko_byte *byte = &system->memory_map[i];
        byte->base_index = (byte->raw_value % 12u) + 1;
        init_cubit_ring(byte);
        lookup_superposition(byte->base_index,
                             &byte->primary_superposition,
                             &byte->secondary_superposition);
    }
    return MMUKO_BOOT_OK;
}

static mmuko_boot_status phase2(mmuko_system *system)
{
    for (size_t b = 0; b < system->memory_size; b++) {
        for (int i = 0; i < 8; i++) {
            if (system->memory_map[b].cubit_ring[i].direction == MMUKO_DIR_UNDEFINED)
                return MMUKO_BOOT_LOCK_DETECTED;
        }
    }
    return MMUKO_BOOT_OK;
}

static mmuko_boot_status phase3(mmuko_system *system)
{
    for (size_t b = 0; b < system->memory_size; b++) {
        mmuko_byte *byte = &system->memory_map[b];
        for (int i = 0; i < 8; i++) {
            mmuko_cubit *cubit = &byte->cubit_ring[i];
            int partner_index = cubit->entangled_with;
            if (!cubit->superposed || partner_index < 0) continue;
            mmuko_cubit *partner = &byte->cubit_ring[partner_index];
            if (cubit->state == partner->state) partner->state = flip_state(partner->state);
        }
    }
    return MMUKO_BOOT_OK;
}

static mmuko_boot_status phase4(mmuko_system *system)
{
    mmuko_direction primary, secondary;
    lookup_superposition(6, &primary, &secondary);
    system->frame_of_reference = primary;
    for (size_t i = 0; i < system->memory_size; i++) {
        system->memory_map[i].primary_superposition = primary;
        system->memory_map[i].secondary_superposition = secondary;
    }
    return MMUKO_BOOT_OK;
}

static mmuko_boot_status phase5(mmuko_system *system)
{
    static const int order[] = {12, 6, 8, 4, 10, 2, 1};
    for (size_t oi = 0; oi < ARRAY_LEN(order); oi++) {
        mmuko_direction primary, secondary;
        lookup_superposition(order[oi], &primary, &secondary);
        for (size_t i = 0; i < system->memory_size; i++) {
            if (system->memory_map[i].base_index == order[oi]) {
                system->memory_map[i].primary_superposition = primary;
                system->memory_map[i].secondary_superposition = secondary;
            }
        }
    }
    return MMUKO_BOOT_OK;
}

static uint8_t rotate8(uint8_t value, unsigned n)
{
    n &= 7u;
    if (n == 0u) return value;
    return (uint8_t)((value >> n) | (uint8_t)(value << (8u - n)));
}

static mmuko_boot_status phase6(mmuko_system *system)
{
    for (size_t b = 0; b < system->memory_size; b++) {
        for (int i = 0; i < 8; i++) {
            uint8_t value = system->memory_map[b].cubit_ring[i].value;
            if (rotate8(rotate8(value, 4), 4) != value) return MMUKO_BOOT_ROTATION_LOCK;
        }
    }
    return MMUKO_BOOT_OK;
}

void mmuko_system_init(mmuko_system *system, mmuko_byte *memory, size_t memory_size)
{
    system->memory_map = memory;
    system->memory_size = memory_size;
    system->medium.gravity_milli = 9800;
    system->medium.air_milli = 0;
    system->medium.water_milli = 0;
    system->frame_of_reference = MMUKO_DIR_N;
    system->boot_complete = false;
    for (size_t i = 0; i < memory_size; i++) memory[i].raw_value = (uint8_t)(i * 17u + 42u);
}

mmuko_boot_status mmuko_boot(mmuko_system *system)
{
    mmuko_boot_status status;
    if (!system || !system->memory_map || system->memory_size == 0u) return MMUKO_BOOT_FAILED;
    if ((status = phase1(system)) != MMUKO_BOOT_OK) return status;
    if ((status = phase2(system)) != MMUKO_BOOT_OK) return status;
    if ((status = phase3(system)) != MMUKO_BOOT_OK) return status;
    if ((status = phase4(system)) != MMUKO_BOOT_OK) return status;
    if ((status = phase5(system)) != MMUKO_BOOT_OK) return status;
    if ((status = phase6(system)) != MMUKO_BOOT_OK) return status;
    system->boot_complete = true;
    return MMUKO_BOOT_OK;
}

uint32_t mmuko_system_checksum(const mmuko_system *system)
{
    uint32_t checksum = 0x4d4d554bu;
    if (!system) return 0u;
    for (size_t i = 0; i < system->memory_size; i++) {
        checksum ^= system->memory_map[i].raw_value;
        checksum = (checksum << 5) | (checksum >> 27);
        checksum += (uint32_t)system->memory_map[i].base_index;
    }
    return checksum;
}
