#ifndef MMUKO_SYSTEM_H
#define MMUKO_SYSTEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MMUKO_MEMORY_SIZE 16u

typedef enum mmuko_direction {
    MMUKO_DIR_N,
    MMUKO_DIR_NE,
    MMUKO_DIR_E,
    MMUKO_DIR_SE,
    MMUKO_DIR_S,
    MMUKO_DIR_SW,
    MMUKO_DIR_W,
    MMUKO_DIR_NW,
    MMUKO_DIR_UNDEFINED
} mmuko_direction;

typedef enum mmuko_state {
    MMUKO_STATE_UP,
    MMUKO_STATE_DOWN,
    MMUKO_STATE_CHARM,
    MMUKO_STATE_STRANGE,
    MMUKO_STATE_LEFT,
    MMUKO_STATE_RIGHT
} mmuko_state;

typedef enum mmuko_boot_status {
    MMUKO_BOOT_OK = 0,
    MMUKO_BOOT_LOCK_DETECTED = 1,
    MMUKO_BOOT_ROTATION_LOCK = 2,
    MMUKO_BOOT_FAILED = 3
} mmuko_boot_status;

typedef struct mmuko_cubit {
    int index;
    uint8_t value;
    uint16_t spin_mrad;
    mmuko_direction direction;
    mmuko_state state;
    bool superposed;
    int entangled_with;
} mmuko_cubit;

typedef struct mmuko_byte {
    uint8_t raw_value;
    mmuko_cubit cubit_ring[8];
    int base_index;
    mmuko_direction primary_superposition;
    mmuko_direction secondary_superposition;
} mmuko_byte;

typedef struct mmuko_vacuum_medium {
    uint16_t gravity_milli;
    uint16_t air_milli;
    uint16_t water_milli;
} mmuko_vacuum_medium;

typedef struct mmuko_system {
    mmuko_byte *memory_map;
    size_t memory_size;
    mmuko_vacuum_medium medium;
    mmuko_direction frame_of_reference;
    bool boot_complete;
} mmuko_system;

void mmuko_system_init(mmuko_system *system, mmuko_byte *memory, size_t memory_size);
mmuko_boot_status mmuko_boot(mmuko_system *system);
uint32_t mmuko_system_checksum(const mmuko_system *system);
const char *mmuko_direction_name(mmuko_direction direction);

#endif
