/* NAOMI: the save file (settings and records, save_data.h) in the board's battery-backed SRAM instead of a VMU.
 *
 * The SRAM is 32 KB at 0x00200000 (0xA0200000 uncached), byte-wide. It is shared with the other homebrew on the same
 * board (0skarzer0/naomi-wheel-games): Cannonball keeps its settings at 0x0000-0x0FFF, the recompiled Daytona
 * (platform/dreamcast on the naomi branch) its EEPROM and backup RAM at 0x1000-0x5FFF. This port takes 0x6000-0x7FFF:
 *
 *   0x6000  "DTN1"            magic, written last
 *   0x6004  u32 size          bytes of the file
 *   0x6008  u32 checksum      of the file's bytes
 *   0x600C  u32 reserved
 *   0x6010  the file          as nm_save_store_write builds it for a VMU (VMS header, icon, payload), whole blocks
 *
 * The game's save code is unchanged: this is one more nm_save_device_t (save_store.h) with a single "card" that
 * holds a single file. The writer runs on its own thread (game_save.c), never in the race. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "naomi.h"
#include "save_store.h"

#define SRAM_BASE ((volatile uint8_t *)0xA0206000u)
#define SRAM_BYTES 0x2000u
#define HEADER 16u
#define FILE_MAX (SRAM_BYTES - HEADER)
#define BLOCK 512u

static uint32_t checksum(const uint8_t *d, size_t n) {
    uint32_t s = 0x44544e31u; /* "DTN1" */
    for (size_t i = 0; i < n; ++i) s = (s << 5) + s + d[i];
    return s;
}
static uint32_t get32(size_t at) {
    return (uint32_t)SRAM_BASE[at] | (uint32_t)SRAM_BASE[at + 1] << 8 | (uint32_t)SRAM_BASE[at + 2] << 16 |
           (uint32_t)SRAM_BASE[at + 3] << 24;
}
static void put32(size_t at, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) SRAM_BASE[at + i] = (uint8_t)(v >> (8 * i));
}
/* The stored file's size, 0 when there is none whole. */
static size_t stored_size(void) {
    if (SRAM_BASE[0] != 'D' || SRAM_BASE[1] != 'T' || SRAM_BASE[2] != 'N' || SRAM_BASE[3] != '1') return 0;
    const uint32_t size = get32(4);
    if (!size || size > FILE_MAX) return 0;
    return size;
}

static void *sram_find(void *context) { (void)context; return (void *)SRAM_BASE; }
static int sram_free_blocks(void *context, void *card) {
    (void)context; (void)card;
    const size_t used = (stored_size() + BLOCK - 1) / BLOCK;
    return (int)(FILE_MAX / BLOCK - used);
}
static int sram_file_blocks(void *context, void *card, const char *name) {
    (void)context; (void)card; (void)name;
    return (int)((stored_size() + BLOCK - 1) / BLOCK);
}
static int sram_read(void *context, void *card, const char *name, uint8_t **data, size_t *size) {
    (void)context; (void)card; (void)name;
    const size_t bytes = stored_size();
    if (!bytes) return -1;
    uint8_t *buffer = malloc(bytes);
    if (!buffer) return -1;
    for (size_t i = 0; i < bytes; ++i) buffer[i] = SRAM_BASE[HEADER + i];
    if (checksum(buffer, bytes) != get32(8)) { free(buffer); return -1; } /* a damaged file: the next save replaces it */
    *data = buffer;
    *size = bytes;
    return 0;
}
static void sram_release(void *context, uint8_t *data) { (void)context; free(data); }
static int sram_write(void *context, void *card, const char *name, const uint8_t *data, size_t size) {
    (void)context; (void)card; (void)name;
    if (!size || size > FILE_MAX) return -1;
    SRAM_BASE[0] = 0; /* no valid magic while writing: a power cut leaves "no file", never half of one */
    for (size_t i = 0; i < size; ++i) SRAM_BASE[HEADER + i] = data[i];
    put32(4, (uint32_t)size);
    put32(8, checksum(data, size));
    put32(12, 0);
    SRAM_BASE[3] = '1';
    SRAM_BASE[2] = 'N';
    SRAM_BASE[1] = 'T';
    SRAM_BASE[0] = 'D';
    for (size_t i = 0; i < size; ++i)
        if (SRAM_BASE[HEADER + i] != data[i]) return -1;
    return 0;
}

static const nm_save_device_t sram = {NULL, sram_find, sram_free_blocks, sram_file_blocks,
                                      sram_read, sram_release, sram_write};

const void *nm_naomi_save_device(void) { return &sram; }
