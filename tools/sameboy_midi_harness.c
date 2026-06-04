#define GB_INTERNAL

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Core/gb.h"
#include "Core/apu.h"
#include "Core/memory.h"

#define FNV64_OFFSET 1469598103934665603ull
#define FNV64_PRIME 1099511628211ull
#define TICKS_PER_FRAME 139810ull
#define MAX_INSTANCES 4
#define MGB_MIDI_CHANNEL_BASE 28
#define MGB_MIDI_CHANNEL_COUNT 5
#define MGB_GLOBAL_SAVE_COUNT 16
#define MGB_GLOBAL_SAVE_MAGIC 0x6D
#define MGB_GLOBAL_SAVE_VERSION 0x01
#define MAX_DATA_SET_PATCHES 32

#define JOY_A      0x0001u
#define JOY_B      0x0002u
#define JOY_SELECT 0x0004u
#define JOY_START  0x0008u
#define JOY_RIGHT  0x0010u
#define JOY_LEFT   0x0020u
#define JOY_UP     0x0040u
#define JOY_DOWN   0x0080u

typedef struct {
    unsigned sound_writes;
    unsigned serial_register_writes;
    unsigned trigger_pu1;
    unsigned trigger_pu2;
    unsigned trigger_wav;
    unsigned trigger_noi;
    unsigned logs;
    unsigned audio_samples;
    uint64_t sound_hash;
    uint64_t audio_hash;
    uint64_t audio_energy;
    uint64_t log_hash;
} HarnessState;

typedef struct {
    GB_gameboy_t *gb;
    HarnessState state;
    uint32_t pixels[256 * 224];
    const struct HarnessConfig *config;
    int timed_out;
    int save_fixture_applied;
    int channel_map_applied;
    int data_set_patch_applied;
    int joypad_script_ran;
} HarnessInstance;

typedef struct HarnessConfig {
    const char *rom;
    const char *boot;
    const char *mode;
    const char *label;
    const char *bytes_text;
    const char *channel_map_text;
    const char *data_set_patch_text;
    const char *joypad_script;
    const char *sram_fixture;
    const char *save_fixture;
    uint16_t save_data_addr;
    uint16_t check_memory_addr;
    uint16_t data_set_addr;
    uint8_t channel_map[MGB_MIDI_CHANNEL_COUNT];
    unsigned channel_map_count;
    uint8_t data_set_patch_offset[MAX_DATA_SET_PATCHES];
    uint8_t data_set_patch_value[MAX_DATA_SET_PATCHES];
    unsigned data_set_patch_count;
    unsigned instances;
    unsigned warmup_frames;
    unsigned settle_frames;
    unsigned bit_ticks;
    unsigned byte_ticks;
    unsigned press_start;
    unsigned start_delay_frames;
    unsigned start_hold_frames;
    unsigned post_start_frames;
    unsigned joypad_hold_frames;
    unsigned joypad_release_frames;
    uint64_t serial_wait_ticks;
} HarnessConfig;

static uint64_t fnv64_byte(uint64_t hash, uint8_t byte)
{
    hash ^= byte;
    hash *= FNV64_PRIME;
    return hash;
}

static uint64_t fnv64_u16(uint64_t hash, uint16_t value)
{
    hash = fnv64_byte(hash, (uint8_t)(value >> 8));
    return fnv64_byte(hash, (uint8_t)value);
}

static void reset_state(HarnessState *state)
{
    memset(state, 0, sizeof(*state));
    state->sound_hash = FNV64_OFFSET;
    state->audio_hash = FNV64_OFFSET;
    state->log_hash = FNV64_OFFSET;
}

static uint32_t rgb_encode(GB_gameboy_t *gb, uint8_t r, uint8_t g, uint8_t b)
{
    (void)gb;
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8);
}

static void log_callback(GB_gameboy_t *gb, const char *string, GB_log_attributes_t attributes)
{
    HarnessInstance *instance = GB_get_user_data(gb);
    (void)attributes;
    instance->state.logs++;
    for (const unsigned char *p = (const unsigned char *)string; *p; p++) {
        instance->state.log_hash = fnv64_byte(instance->state.log_hash, *p);
    }
}

static bool write_callback(GB_gameboy_t *gb, uint16_t addr, uint8_t data)
{
    HarnessInstance *instance = GB_get_user_data(gb);
    HarnessState *state = &instance->state;

    if (addr == 0xFF01 || addr == 0xFF02) {
        state->serial_register_writes++;
    }

    if (addr >= 0xFF10 && addr <= 0xFF3F) {
        state->sound_writes++;
        state->sound_hash = fnv64_u16(state->sound_hash, addr);
        state->sound_hash = fnv64_byte(state->sound_hash, data);

        if (data & 0x80) {
            switch (addr) {
                case 0xFF14:
                    state->trigger_pu1++;
                    break;
                case 0xFF19:
                    state->trigger_pu2++;
                    break;
                case 0xFF1E:
                    state->trigger_wav++;
                    break;
                case 0xFF23:
                    state->trigger_noi++;
                    break;
            }
        }
    }

    return true;
}

static void sample_callback(GB_gameboy_t *gb, GB_sample_t *sample)
{
    HarnessInstance *instance = GB_get_user_data(gb);
    HarnessState *state = &instance->state;
    int left = sample->left;
    int right = sample->right;

    state->audio_samples++;
    state->audio_energy += (uint64_t)(left < 0 ? -left : left);
    state->audio_energy += (uint64_t)(right < 0 ? -right : right);
    state->audio_hash = fnv64_u16(state->audio_hash, (uint16_t)sample->left);
    state->audio_hash = fnv64_u16(state->audio_hash, (uint16_t)sample->right);
}

static void run_ticks(GB_gameboy_t *gb, uint64_t ticks)
{
    uint64_t elapsed = 0;
    unsigned zero_runs = 0;

    while (elapsed < ticks) {
        unsigned ran = GB_run(gb);
        elapsed += ran;
        if (ran == 0 && ++zero_runs > 1000000) {
            break;
        }
    }
}

static void run_all_ticks(HarnessInstance *instances, unsigned count, uint64_t ticks)
{
    for (unsigned i = 0; i < count; i++) {
        run_ticks(instances[i].gb, ticks);
    }
}

static bool serial_ready_for_external_clock(GB_gameboy_t *gb)
{
    uint8_t sc = gb->io_registers[GB_IO_SC];
    return (sc & 0x80) && !(sc & 0x01);
}

static int wait_all_external_serial_ready(HarnessInstance *instances, unsigned count, uint64_t max_ticks)
{
    uint64_t elapsed = 0;

    while (elapsed < max_ticks) {
        bool all_ready = true;
        for (unsigned i = 0; i < count; i++) {
            if (!serial_ready_for_external_clock(instances[i].gb)) {
                all_ready = false;
                break;
            }
        }
        if (all_ready) {
            return 0;
        }

        for (unsigned i = 0; i < count; i++) {
            elapsed += GB_run(instances[i].gb);
        }
    }

    return -1;
}

static int inject_serial_byte_all(HarnessInstance *instances, unsigned count, uint8_t value, const HarnessConfig *config)
{
    for (int bit = 7; bit >= 0; bit--) {
        if (wait_all_external_serial_ready(instances, count, config->serial_wait_ticks)) {
            for (unsigned i = 0; i < count; i++) {
                instances[i].timed_out = !serial_ready_for_external_clock(instances[i].gb);
            }
            return -1;
        }
        for (unsigned i = 0; i < count; i++) {
            GB_serial_set_data_bit(instances[i].gb, (value >> bit) & 1);
        }
        run_all_ticks(instances, count, config->bit_ticks);
    }

    run_all_ticks(instances, count, config->byte_ticks);
    return 0;
}

static int parse_hex_bytes(const char *text, uint8_t *bytes, size_t capacity, size_t *length)
{
    const char *p = text;
    *length = 0;

    while (*p) {
        while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }
        if (!*p) {
            break;
        }
        if (*length == capacity) {
            fprintf(stderr, "too many MIDI bytes; capacity is %zu\n", capacity);
            return -1;
        }

        char *end = NULL;
        unsigned long byte = strtoul(p, &end, 16);
        if (end == p || byte > 0xFF) {
            fprintf(stderr, "bad MIDI byte near: %s\n", p);
            return -1;
        }

        bytes[(*length)++] = (uint8_t)byte;
        p = end;
    }

    return 0;
}

static int parse_channel_map(const char *text, uint8_t *channels, unsigned *count)
{
    const char *p = text;
    *count = 0;

    while (*p) {
        while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }
        if (!*p) {
            break;
        }
        if (*count == MGB_MIDI_CHANNEL_COUNT) {
            fprintf(stderr, "too many MIDI channel entries; expected %u\n", MGB_MIDI_CHANNEL_COUNT);
            return -1;
        }

        char *end = NULL;
        unsigned long value = strtoul(p, &end, 0);
        if (end == p || value > 0x0F) {
            fprintf(stderr, "bad MIDI channel nibble near: %s\n", p);
            return -1;
        }

        channels[(*count)++] = (uint8_t)value;
        p = end;
    }

    if (*count && *count != MGB_MIDI_CHANNEL_COUNT) {
        fprintf(stderr, "MIDI channel map needs exactly %u entries\n", MGB_MIDI_CHANNEL_COUNT);
        return -1;
    }
    return 0;
}

static int parse_data_set_patch(const char *text, uint8_t *offsets, uint8_t *values, unsigned *count)
{
    const char *p = text;
    *count = 0;

    while (*p) {
        while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }
        if (!*p) {
            break;
        }
        if (*count == MAX_DATA_SET_PATCHES) {
            fprintf(stderr, "too many dataSet patches; capacity is %u\n", MAX_DATA_SET_PATCHES);
            return -1;
        }

        char *end = NULL;
        unsigned long offset = strtoul(p, &end, 0);
        if (end == p || offset > 0xFF || *end != '=') {
            fprintf(stderr, "bad dataSet patch near: %s\n", p);
            return -1;
        }
        p = end + 1;
        unsigned long value = strtoul(p, &end, 0);
        if (end == p || value > 0xFF) {
            fprintf(stderr, "bad dataSet patch value near: %s\n", p);
            return -1;
        }
        offsets[*count] = (uint8_t)offset;
        values[*count] = (uint8_t)value;
        (*count)++;
        p = end;
    }
    return 0;
}

static uint64_t final_apu_hash(GB_gameboy_t *gb)
{
    uint64_t hash = FNV64_OFFSET;
    for (uint16_t addr = 0xFF10; addr <= 0xFF3F; addr++) {
        hash = fnv64_byte(hash, gb->io_registers[addr - 0xFF00]);
    }
    return hash;
}

static uint8_t mGB_save_checksum(uint8_t *ram, size_t size)
{
    uint8_t checksum = 0x5A;
    if (size < 514) {
        return 0;
    }

    for (unsigned x = 0; x != 128; x += 8) {
        for (unsigned i = 0; i < 7; i++) checksum = (uint8_t)((checksum << 1) ^ ram[x + i] ^ 0x11);
        for (unsigned i = 0; i < 6; i++) checksum = (uint8_t)((checksum << 1) ^ ram[128 + x + i] ^ 0x22);
        for (unsigned i = 0; i < 7; i++) checksum = (uint8_t)((checksum << 1) ^ ram[256 + x + i] ^ 0x44);
        for (unsigned i = 0; i < 4; i++) checksum = (uint8_t)((checksum << 1) ^ ram[384 + x + i] ^ 0x88);
    }
    return checksum;
}

static const uint16_t mGB_global_save_offsets[MGB_GLOBAL_SAVE_COUNT] = {
    7, 15, 23, 31, 39, 47, 55, 63,
    71, 79, 87, 95, 103, 111, 119, 127,
};

static uint8_t mGB_global_checksum(uint8_t *ram, size_t size)
{
    uint8_t checksum = MGB_GLOBAL_SAVE_MAGIC ^ MGB_GLOBAL_SAVE_VERSION;
    if (size < 514) {
        return 0;
    }
    for (unsigned i = 3; i != 14; i++) {
        checksum = (uint8_t)((checksum << 1) ^ ram[mGB_global_save_offsets[i]] ^ 0x33);
    }
    return checksum;
}

static void seed_global_config(
    uint8_t *ram,
    size_t size,
    const uint8_t channels[MGB_MIDI_CHANNEL_COUNT],
    uint8_t base,
    uint8_t profile,
    uint8_t mpe,
    uint8_t velocity_curve,
    uint8_t tuning,
    uint8_t legato
)
{
    if (size < 514) {
        return;
    }
    ram[mGB_global_save_offsets[0]] = MGB_GLOBAL_SAVE_MAGIC;
    ram[mGB_global_save_offsets[1]] = MGB_GLOBAL_SAVE_VERSION;
    for (unsigned i = 0; i < MGB_MIDI_CHANNEL_COUNT; i++) {
        ram[mGB_global_save_offsets[3 + i]] = channels[i];
    }
    ram[mGB_global_save_offsets[8]] = base;
    ram[mGB_global_save_offsets[9]] = profile;
    ram[mGB_global_save_offsets[10]] = mpe;
    ram[mGB_global_save_offsets[11]] = velocity_curve;
    ram[mGB_global_save_offsets[12]] = tuning;
    ram[mGB_global_save_offsets[13]] = legato;
    ram[mGB_global_save_offsets[14]] = 0;
    ram[mGB_global_save_offsets[15]] = 0;
    ram[mGB_global_save_offsets[2]] = mGB_global_checksum(ram, size);
}

static void seed_valid_sram(uint8_t *ram, size_t size, uint8_t checksum_override)
{
    static const uint8_t defaults[24] = {
        0x02, 0x00, 0x06, 0x00, 0x02, 0x00, 0x03,
        0x02, 0x00, 0x06, 0x02, 0x00, 0x03,
        0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x03,
        0x02, 0x06, 0x00, 0x03,
    };

    memset(ram, 0, size);
    if (size < 514) {
        return;
    }

    for (unsigned x = 0; x != 128; x += 8) {
        unsigned l = 0;
        for (unsigned i = 0; i < 7; i++) ram[x + l++] = defaults[i];
        l = 0;
        for (unsigned i = 7; i != 13; i++) ram[128 + x + l++] = defaults[i];
        l = 0;
        for (unsigned i = 13; i != 20; i++) ram[256 + x + l++] = defaults[i];
        l = 0;
        for (unsigned i = 20; i != 24; i++) ram[384 + x + l++] = defaults[i];
    }

    ram[512] = 0xF7;
    ram[513] = checksum_override == 0xFE ? mGB_save_checksum(ram, size) : checksum_override;
    {
        const uint8_t channels[MGB_MIDI_CHANNEL_COUNT] = {0, 1, 2, 3, 4};
        seed_global_config(ram, size, channels, 0, 0, 0, 0, 0, 0);
    }
}

static void apply_sram_fixture(GB_gameboy_t *gb, const char *fixture)
{
    size_t size = 0;
    uint8_t *ram = GB_get_direct_access(gb, GB_DIRECT_ACCESS_CART_RAM, &size, NULL);
    if (!ram || !size || strcmp(fixture, "none") == 0) {
        return;
    }

    if (strcmp(fixture, "empty") == 0) {
        memset(ram, 0x00, size);
    }
    else if (strcmp(fixture, "ff") == 0) {
        memset(ram, 0xFF, size);
    }
    else if (strcmp(fixture, "pattern") == 0) {
        for (size_t i = 0; i < size; i++) {
            ram[i] = (uint8_t)((i * 37u + 0x55u) & 0xFFu);
        }
    }
    else if (strcmp(fixture, "valid") == 0) {
        seed_valid_sram(ram, size, 0xFE);
    }
    else if (strcmp(fixture, "valid_global_map") == 0) {
        const uint8_t channels[MGB_MIDI_CHANNEL_COUNT] = {8, 9, 10, 11, 12};
        seed_valid_sram(ram, size, 0xFE);
        seed_global_config(ram, size, channels, 8, 0, 0, 0, 0, 0);
    }
    else if (strcmp(fixture, "valid_global_profile3") == 0) {
        const uint8_t channels[MGB_MIDI_CHANNEL_COUNT] = {0, 1, 2, 3, 4};
        seed_valid_sram(ram, size, 0xFE);
        seed_global_config(ram, size, channels, 8, 3, 0, 0, 0, 0);
    }
    else if (strcmp(fixture, "valid_empty_checksum") == 0) {
        seed_valid_sram(ram, size, 0x00);
    }
    else if (strcmp(fixture, "corrupt_checksum") == 0) {
        seed_valid_sram(ram, size, 0xFE);
        if (size >= 514) {
            ram[513] ^= 0x55;
        }
    }
}

static uint8_t *wram_ptr_for_addr(GB_gameboy_t *gb, uint16_t addr, size_t length)
{
    size_t size = 0;
    uint8_t *ram = GB_get_direct_access(gb, GB_DIRECT_ACCESS_RAM, &size, NULL);
    if (!ram || addr < 0xC000 || (size_t)(addr - 0xC000) + length > size) {
        return NULL;
    }
    return ram + (addr - 0xC000);
}

static uint8_t *save_ptr_for_addr(GB_gameboy_t *gb, uint16_t addr, size_t length)
{
    if (addr >= 0xC000) {
        return wram_ptr_for_addr(gb, addr, length);
    }
    if (addr >= 0xA000 && addr < 0xC000) {
        size_t size = 0;
        uint8_t *ram = GB_get_direct_access(gb, GB_DIRECT_ACCESS_CART_RAM, &size, NULL);
        if (!ram || (size_t)(addr - 0xA000) + length > size) {
            return NULL;
        }
        return ram + (addr - 0xA000);
    }
    return NULL;
}

static void apply_channel_map(HarnessInstance *instance)
{
    const HarnessConfig *config = instance->config;
    if (!config || !config->data_set_addr || config->channel_map_count != MGB_MIDI_CHANNEL_COUNT) {
        return;
    }

    uint8_t *data_set = wram_ptr_for_addr(
        instance->gb,
        config->data_set_addr,
        MGB_MIDI_CHANNEL_BASE + MGB_MIDI_CHANNEL_COUNT
    );
    if (!data_set) {
        return;
    }

    for (unsigned i = 0; i < MGB_MIDI_CHANNEL_COUNT; i++) {
        data_set[MGB_MIDI_CHANNEL_BASE + i] = config->channel_map[i] & 0x0F;
    }
    instance->channel_map_applied = 1;
}

static void apply_data_set_patch(HarnessInstance *instance)
{
    const HarnessConfig *config = instance->config;
    if (!config || !config->data_set_addr || !config->data_set_patch_count) {
        return;
    }

    uint8_t *data_set = wram_ptr_for_addr(instance->gb, config->data_set_addr, 64);
    if (!data_set) {
        return;
    }

    for (unsigned i = 0; i < config->data_set_patch_count; i++) {
        data_set[config->data_set_patch_offset[i]] = config->data_set_patch_value[i];
    }
    instance->data_set_patch_applied = 1;
}

static void seed_save_fixture(uint8_t *save_data, const char *fixture)
{
    if (strcmp(fixture, "empty") == 0) {
        memset(save_data, 0x00, 514);
    }
    else if (strcmp(fixture, "ff") == 0) {
        memset(save_data, 0xFF, 514);
    }
    else if (strcmp(fixture, "pattern") == 0) {
        for (unsigned i = 0; i < 514; i++) {
            save_data[i] = (uint8_t)((i * 37u + 0x55u) & 0xFFu);
        }
    }
    else if (strcmp(fixture, "valid") == 0) {
        seed_valid_sram(save_data, 514, 0xFE);
    }
    else if (strcmp(fixture, "valid_global_map") == 0) {
        const uint8_t channels[MGB_MIDI_CHANNEL_COUNT] = {8, 9, 10, 11, 12};
        seed_valid_sram(save_data, 514, 0xFE);
        seed_global_config(save_data, 514, channels, 8, 0, 0, 0, 0, 0);
    }
    else if (strcmp(fixture, "valid_global_profile3") == 0) {
        const uint8_t channels[MGB_MIDI_CHANNEL_COUNT] = {0, 1, 2, 3, 4};
        seed_valid_sram(save_data, 514, 0xFE);
        seed_global_config(save_data, 514, channels, 8, 3, 0, 0, 0, 0);
    }
    else if (strcmp(fixture, "valid_empty_checksum") == 0) {
        seed_valid_sram(save_data, 514, 0x00);
    }
    else if (strcmp(fixture, "corrupt_checksum") == 0) {
        seed_valid_sram(save_data, 514, 0xFE);
        save_data[513] ^= 0x55;
    }
}

static void apply_save_fixture(HarnessInstance *instance)
{
    const HarnessConfig *config = instance->config;
    if (!config || strcmp(config->save_fixture, "none") == 0 || instance->save_fixture_applied) {
        return;
    }
    if (!config->save_data_addr || !config->check_memory_addr) {
        return;
    }

    uint8_t *save_data = save_ptr_for_addr(instance->gb, config->save_data_addr, 514);
    if (!save_data) {
        return;
    }
    seed_save_fixture(save_data, config->save_fixture);
    instance->save_fixture_applied = 1;
}

static void execution_callback(GB_gameboy_t *gb, uint16_t address, uint8_t opcode)
{
    HarnessInstance *instance = GB_get_user_data(gb);
    (void)opcode;
    if (instance->config && address == instance->config->check_memory_addr) {
        apply_save_fixture(instance);
    }
}

static uint64_t sram_hash(GB_gameboy_t *gb)
{
    size_t size = 0;
    uint8_t *ram = GB_get_direct_access(gb, GB_DIRECT_ACCESS_CART_RAM, &size, NULL);
    uint64_t hash = FNV64_OFFSET;
    size_t limit = size < 514 ? size : 514;

    for (size_t i = 0; i < limit; i++) {
        hash = fnv64_byte(hash, ram[i]);
    }
    return hash;
}

static uint8_t *save_data_ptr(HarnessInstance *instance)
{
    if (!instance->config || !instance->config->save_data_addr) {
        return NULL;
    }
    return save_ptr_for_addr(instance->gb, instance->config->save_data_addr, 514);
}

static uint64_t save_hash(HarnessInstance *instance)
{
    uint8_t *save_data = save_data_ptr(instance);
    uint64_t hash = FNV64_OFFSET;
    if (!save_data) {
        return hash;
    }
    for (unsigned i = 0; i < 514; i++) {
        hash = fnv64_byte(hash, save_data[i]);
    }
    return hash;
}

static uint8_t save_byte(HarnessInstance *instance, unsigned offset)
{
    uint8_t *save_data = save_data_ptr(instance);
    if (!save_data || offset >= 514) {
        return 0;
    }
    return save_data[offset];
}

static unsigned save_valid(HarnessInstance *instance)
{
    uint8_t *save_data = save_data_ptr(instance);
    if (!save_data) {
        return 0;
    }
    return save_data[512] == 0xF7 && save_data[513] == mGB_save_checksum(save_data, 514);
}

static unsigned sram_valid(GB_gameboy_t *gb)
{
    size_t size = 0;
    uint8_t *ram = GB_get_direct_access(gb, GB_DIRECT_ACCESS_CART_RAM, &size, NULL);
    if (!ram || size < 514) {
        return 0;
    }
    return ram[512] == 0xF7 && ram[513] == mGB_save_checksum(ram, size);
}

static uint8_t sram_byte(GB_gameboy_t *gb, size_t offset)
{
    size_t size = 0;
    uint8_t *ram = GB_get_direct_access(gb, GB_DIRECT_ACCESS_CART_RAM, &size, NULL);
    if (!ram || offset >= size) {
        return 0;
    }
    return ram[offset];
}

static size_t sram_size(GB_gameboy_t *gb)
{
    size_t size = 0;
    GB_get_direct_access(gb, GB_DIRECT_ACCESS_CART_RAM, &size, NULL);
    return size;
}

static GB_model_t model_from_mode(const char *mode)
{
    if (strcmp(mode, "dmg") == 0) {
        return GB_MODEL_DMG_B;
    }
    return GB_MODEL_CGB_E;
}

static void set_start_for_all(HarnessInstance *instances, unsigned count, bool pressed)
{
    for (unsigned i = 0; i < count; i++) {
        GB_set_key_state(instances[i].gb, GB_KEY_START, pressed);
    }
}

static int key_bit_from_name(const char *name, size_t length)
{
    if (length == 1 && name[0] == 'a') return JOY_A;
    if (length == 1 && name[0] == 'b') return JOY_B;
    if (length == 2 && name[0] == 'u' && name[1] == 'p') return JOY_UP;
    if (length == 4 && memcmp(name, "down", 4) == 0) return JOY_DOWN;
    if (length == 4 && memcmp(name, "left", 4) == 0) return JOY_LEFT;
    if (length == 5 && memcmp(name, "right", 5) == 0) return JOY_RIGHT;
    if (length == 5 && memcmp(name, "start", 5) == 0) return JOY_START;
    if (length == 6 && memcmp(name, "select", 6) == 0) return JOY_SELECT;
    return 0;
}

static int parse_joypad_combo(const char *token, uint16_t *mask)
{
    const char *part = token;
    *mask = 0;

    while (*part) {
        const char *end = part;
        while (*end && *end != '+') {
            end++;
        }
        int bit = key_bit_from_name(part, (size_t)(end - part));
        if (!bit) {
            fprintf(stderr, "bad joypad token: %s\n", token);
            return -1;
        }
        *mask |= (uint16_t)bit;
        part = *end == '+' ? end + 1 : end;
    }

    return *mask ? 0 : -1;
}

static void set_key_mask_all(HarnessInstance *instances, unsigned count, uint16_t mask, bool pressed)
{
    for (unsigned i = 0; i < count; i++) {
        GB_gameboy_t *gb = instances[i].gb;
        if (mask & JOY_A) GB_set_key_state(gb, GB_KEY_A, pressed);
        if (mask & JOY_B) GB_set_key_state(gb, GB_KEY_B, pressed);
        if (mask & JOY_SELECT) GB_set_key_state(gb, GB_KEY_SELECT, pressed);
        if (mask & JOY_START) GB_set_key_state(gb, GB_KEY_START, pressed);
        if (mask & JOY_RIGHT) GB_set_key_state(gb, GB_KEY_RIGHT, pressed);
        if (mask & JOY_LEFT) GB_set_key_state(gb, GB_KEY_LEFT, pressed);
        if (mask & JOY_UP) GB_set_key_state(gb, GB_KEY_UP, pressed);
        if (mask & JOY_DOWN) GB_set_key_state(gb, GB_KEY_DOWN, pressed);
    }
}

static int run_joypad_script(HarnessInstance *instances, unsigned count, const HarnessConfig *config)
{
    if (!config->joypad_script || !config->joypad_script[0]) {
        return 0;
    }

    char script[2048];
    if (strlen(config->joypad_script) >= sizeof(script)) {
        fprintf(stderr, "joypad script is too long\n");
        return -1;
    }
    strcpy(script, config->joypad_script);

    char *token = strtok(script, ", \t\r\n");
    while (token) {
        if (strncmp(token, "wait:", 5) == 0) {
            unsigned frames = (unsigned)strtoul(token + 5, NULL, 0);
            run_all_ticks(instances, count, TICKS_PER_FRAME * frames);
        }
        else {
            uint16_t mask = 0;
            if (parse_joypad_combo(token, &mask)) {
                return -1;
            }
            set_key_mask_all(instances, count, mask, true);
            run_all_ticks(instances, count, TICKS_PER_FRAME * config->joypad_hold_frames);
            set_key_mask_all(instances, count, mask, false);
            run_all_ticks(instances, count, TICKS_PER_FRAME * config->joypad_release_frames);
        }
        token = strtok(NULL, ", \t\r\n");
    }

    for (unsigned i = 0; i < count; i++) {
        instances[i].joypad_script_ran = 1;
    }
    return 0;
}

static int parse_args(int argc, char **argv, HarnessConfig *config)
{
    *config = (HarnessConfig) {
        .mode = "cgb",
        .label = "scenario",
        .bytes_text = "",
        .channel_map_text = "",
        .data_set_patch_text = "",
        .joypad_script = "",
        .sram_fixture = "none",
        .save_fixture = "none",
        .save_data_addr = 0,
        .check_memory_addr = 0,
        .data_set_addr = 0,
        .channel_map_count = 0,
        .data_set_patch_count = 0,
        .instances = 1,
        .warmup_frames = 600,
        .settle_frames = 120,
        .bit_ticks = 512,
        .byte_ticks = 32768,
        .press_start = 0,
        .start_delay_frames = 60,
        .start_hold_frames = 12,
        .post_start_frames = 120,
        .joypad_hold_frames = 4,
        .joypad_release_frames = 4,
        .serial_wait_ticks = TICKS_PER_FRAME * 4,
    };

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char **target = NULL;

        if (strcmp(arg, "--rom") == 0) target = &config->rom;
        else if (strcmp(arg, "--boot") == 0) target = &config->boot;
        else if (strcmp(arg, "--mode") == 0) target = &config->mode;
        else if (strcmp(arg, "--label") == 0) target = &config->label;
        else if (strcmp(arg, "--bytes") == 0) target = &config->bytes_text;
        else if (strcmp(arg, "--channel-map") == 0) target = &config->channel_map_text;
        else if (strcmp(arg, "--data-set-patch") == 0) target = &config->data_set_patch_text;
        else if (strcmp(arg, "--joypad-script") == 0) target = &config->joypad_script;
        else if (strcmp(arg, "--sram-fixture") == 0) target = &config->sram_fixture;
        else if (strcmp(arg, "--save-fixture") == 0) target = &config->save_fixture;

        if (target) {
            if (++i == argc) {
                fprintf(stderr, "%s needs a value\n", arg);
                return -1;
            }
            *target = argv[i];
            continue;
        }

        if (strcmp(arg, "--press-start") == 0) {
            config->press_start = 1;
            continue;
        }

        if (strcmp(arg, "--instances") == 0 ||
            strcmp(arg, "--warmup-frames") == 0 ||
            strcmp(arg, "--settle-frames") == 0 ||
            strcmp(arg, "--bit-ticks") == 0 ||
            strcmp(arg, "--byte-ticks") == 0 ||
            strcmp(arg, "--start-delay-frames") == 0 ||
            strcmp(arg, "--start-hold-frames") == 0 ||
            strcmp(arg, "--post-start-frames") == 0 ||
            strcmp(arg, "--joypad-hold-frames") == 0 ||
            strcmp(arg, "--joypad-release-frames") == 0 ||
            strcmp(arg, "--save-data-addr") == 0 ||
            strcmp(arg, "--check-memory-addr") == 0 ||
            strcmp(arg, "--data-set-addr") == 0) {
            if (++i == argc) {
                fprintf(stderr, "%s needs a value\n", arg);
                return -1;
            }
            unsigned value = (unsigned)strtoul(argv[i], NULL, 0);
            if (strcmp(arg, "--instances") == 0) config->instances = value;
            else if (strcmp(arg, "--warmup-frames") == 0) config->warmup_frames = value;
            else if (strcmp(arg, "--settle-frames") == 0) config->settle_frames = value;
            else if (strcmp(arg, "--bit-ticks") == 0) config->bit_ticks = value;
            else if (strcmp(arg, "--byte-ticks") == 0) config->byte_ticks = value;
            else if (strcmp(arg, "--start-delay-frames") == 0) config->start_delay_frames = value;
            else if (strcmp(arg, "--start-hold-frames") == 0) config->start_hold_frames = value;
            else if (strcmp(arg, "--post-start-frames") == 0) config->post_start_frames = value;
            else if (strcmp(arg, "--joypad-hold-frames") == 0) config->joypad_hold_frames = value;
            else if (strcmp(arg, "--joypad-release-frames") == 0) config->joypad_release_frames = value;
            else if (strcmp(arg, "--save-data-addr") == 0) config->save_data_addr = (uint16_t)value;
            else if (strcmp(arg, "--check-memory-addr") == 0) config->check_memory_addr = (uint16_t)value;
            else config->data_set_addr = (uint16_t)value;
            continue;
        }

        fprintf(stderr, "unknown argument: %s\n", arg);
        return -1;
    }

    if (!config->rom || !config->boot) {
        fprintf(stderr, "--rom and --boot are required\n");
        return -1;
    }

    if (strcmp(config->mode, "cgb") != 0 && strcmp(config->mode, "dmg") != 0) {
        fprintf(stderr, "--mode must be cgb or dmg\n");
        return -1;
    }

    if (config->instances < 1 || config->instances > MAX_INSTANCES) {
        fprintf(stderr, "--instances must be between 1 and %u\n", MAX_INSTANCES);
        return -1;
    }

    if (config->channel_map_text && config->channel_map_text[0]) {
        if (parse_channel_map(config->channel_map_text, config->channel_map, &config->channel_map_count)) {
            return -1;
        }
        if (!config->data_set_addr) {
            fprintf(stderr, "--channel-map requires --data-set-addr\n");
            return -1;
        }
    }

    if (config->data_set_patch_text && config->data_set_patch_text[0]) {
        if (parse_data_set_patch(
            config->data_set_patch_text,
            config->data_set_patch_offset,
            config->data_set_patch_value,
            &config->data_set_patch_count
        )) {
            return -1;
        }
        if (!config->data_set_addr) {
            fprintf(stderr, "--data-set-patch requires --data-set-addr\n");
            return -1;
        }
    }

    return 0;
}

static unsigned instances_match(HarnessInstance *instances, unsigned count)
{
    if (count <= 1) {
        return 1;
    }

    HarnessState *base = &instances[0].state;
    uint64_t base_apu = final_apu_hash(instances[0].gb);
    uint64_t base_sram = sram_hash(instances[0].gb);
    uint64_t base_save = save_hash(&instances[0]);

    for (unsigned i = 1; i < count; i++) {
        HarnessState *state = &instances[i].state;
        if (instances[i].timed_out != instances[0].timed_out) return 0;
        if (state->sound_writes != base->sound_writes) return 0;
        if (state->serial_register_writes != base->serial_register_writes) return 0;
        if (state->trigger_pu1 != base->trigger_pu1) return 0;
        if (state->trigger_pu2 != base->trigger_pu2) return 0;
        if (state->trigger_wav != base->trigger_wav) return 0;
        if (state->trigger_noi != base->trigger_noi) return 0;
        if (state->logs != base->logs) return 0;
        if (state->sound_hash != base->sound_hash) return 0;
        if (state->audio_hash != base->audio_hash) return 0;
        if (state->audio_energy != base->audio_energy) return 0;
        if (final_apu_hash(instances[i].gb) != base_apu) return 0;
        if (sram_hash(instances[i].gb) != base_sram) return 0;
        if (save_hash(&instances[i]) != base_save) return 0;
    }
    return 1;
}

static void print_instance_details(HarnessInstance *instances, unsigned count)
{
    for (unsigned i = 0; i < count; i++) {
        GB_gameboy_t *gb = instances[i].gb;
        HarnessState *state = &instances[i].state;
        printf("instance%u_timeout=%u\n", i, instances[i].timed_out ? 1 : 0);
        printf("instance%u_sound_writes=%u\n", i, state->sound_writes);
        printf("instance%u_sound_hash=%016llx\n", i, (unsigned long long)state->sound_hash);
        printf("instance%u_trigger_pu1=%u\n", i, state->trigger_pu1);
        printf("instance%u_trigger_pu2=%u\n", i, state->trigger_pu2);
        printf("instance%u_trigger_wav=%u\n", i, state->trigger_wav);
        printf("instance%u_trigger_noi=%u\n", i, state->trigger_noi);
        printf("instance%u_apu_hash=%016llx\n", i, (unsigned long long)final_apu_hash(gb));
        printf("instance%u_audio_hash=%016llx\n", i, (unsigned long long)state->audio_hash);
        printf("instance%u_sram_hash=%016llx\n", i, (unsigned long long)sram_hash(gb));
        printf("instance%u_save_hash=%016llx\n", i, (unsigned long long)save_hash(&instances[i]));
        printf("instance%u_save_valid=%u\n", i, save_valid(&instances[i]));
        printf("instance%u_pc=%04x\n", i, gb->pc);
        printf("instance%u_sp=%04x\n", i, gb->registers[GB_REGISTER_SP]);
    }
}

static uint8_t data_set_byte(HarnessInstance *instance, unsigned offset)
{
    if (!instance->config || !instance->config->data_set_addr) {
        return 0;
    }
    uint8_t *data_set = wram_ptr_for_addr(instance->gb, instance->config->data_set_addr, 64);
    if (!data_set) {
        return 0;
    }
    return data_set[offset];
}

int main(int argc, char **argv)
{
    HarnessConfig config;
    if (parse_args(argc, argv, &config)) {
        return 2;
    }

    uint8_t bytes[8192];
    size_t byte_count = 0;
    if (parse_hex_bytes(config.bytes_text, bytes, sizeof(bytes), &byte_count)) {
        return 2;
    }

    HarnessInstance instances[MAX_INSTANCES];
    memset(instances, 0, sizeof(instances));

    for (unsigned i = 0; i < config.instances; i++) {
        HarnessInstance *instance = &instances[i];
        reset_state(&instance->state);
        instance->gb = GB_init(GB_alloc(), model_from_mode(config.mode));
        instance->config = &config;
        GB_set_user_data(instance->gb, instance);
        GB_set_log_callback(instance->gb, log_callback);
        GB_set_pixels_output(instance->gb, instance->pixels);
        GB_set_rgb_encode_callback(instance->gb, rgb_encode);
        GB_set_turbo_mode(instance->gb, true, true);
        GB_set_turbo_cap(instance->gb, 0);
        GB_set_sample_rate(instance->gb, 48000);
        GB_apu_set_sample_callback(instance->gb, sample_callback);
        GB_set_write_memory_callback(instance->gb, write_callback);
        GB_set_emulate_joypad_bouncing(instance->gb, false);
        if (config.save_data_addr && config.check_memory_addr && strcmp(config.save_fixture, "none") != 0) {
            GB_set_execution_callback(instance->gb, execution_callback);
        }

        if (GB_load_boot_rom(instance->gb, config.boot)) {
            perror("GB_load_boot_rom");
            return 1;
        }
        if (GB_load_rom(instance->gb, config.rom)) {
            perror("GB_load_rom");
            return 1;
        }
        apply_sram_fixture(instance->gb, config.sram_fixture);
    }

    run_all_ticks(instances, config.instances, TICKS_PER_FRAME * config.warmup_frames);
    for (unsigned i = 0; i < config.instances; i++) {
        apply_channel_map(&instances[i]);
        apply_data_set_patch(&instances[i]);
    }
    if (run_joypad_script(instances, config.instances, &config)) {
        return 2;
    }
    for (unsigned i = 0; i < config.instances; i++) {
        reset_state(&instances[i].state);
    }

    int timed_out = 0;
    for (size_t i = 0; i < byte_count; i++) {
        if (inject_serial_byte_all(instances, config.instances, bytes[i], &config)) {
            timed_out = 1;
            break;
        }
    }

    if (config.press_start) {
        run_all_ticks(instances, config.instances, TICKS_PER_FRAME * config.start_delay_frames);
        set_start_for_all(instances, config.instances, true);
        run_all_ticks(instances, config.instances, TICKS_PER_FRAME * config.start_hold_frames);
        set_start_for_all(instances, config.instances, false);
        run_all_ticks(instances, config.instances, TICKS_PER_FRAME * config.post_start_frames);
    }

    run_all_ticks(instances, config.instances, TICKS_PER_FRAME * config.settle_frames);

    HarnessInstance *first = &instances[0];
    GB_gameboy_t *gb = first->gb;
    HarnessState *state = &first->state;
    uint64_t apu_hash = final_apu_hash(gb);
    uint8_t nr12 = gb->io_registers[GB_IO_NR12];
    uint8_t nr13 = gb->io_registers[GB_IO_NR13];
    uint8_t nr14 = gb->io_registers[GB_IO_NR14];
    uint8_t nr22 = gb->io_registers[GB_IO_NR22];
    uint8_t nr32 = gb->io_registers[GB_IO_NR32];
    uint8_t nr42 = gb->io_registers[GB_IO_NR42];
    uint8_t nr51 = gb->io_registers[GB_IO_NR51];
    uint8_t nr52 = gb->io_registers[GB_IO_NR52];
    uint16_t pc = gb->pc;
    uint16_t sp = gb->registers[GB_REGISTER_SP];
    bool boot_finished = gb->boot_rom_finished;
    bool serial_ready = serial_ready_for_external_clock(gb);
    unsigned match = instances_match(instances, config.instances);
    unsigned all_boot_finished = 1;
    unsigned all_serial_ready = 1;
    unsigned all_logs = 0;
    for (unsigned i = 0; i < config.instances; i++) {
        if (!instances[i].gb->boot_rom_finished) all_boot_finished = 0;
        if (!serial_ready_for_external_clock(instances[i].gb)) all_serial_ready = 0;
        all_logs += instances[i].state.logs;
    }

    printf("label=%s\n", config.label);
    printf("mode=%s\n", config.mode);
    printf("instances=%u\n", config.instances);
    printf("instances_match=%u\n", match);
    printf("sram_fixture=%s\n", config.sram_fixture);
    printf("save_fixture=%s\n", config.save_fixture);
    printf("save_fixture_applied=%u\n", first->save_fixture_applied ? 1 : 0);
    printf("channel_map=%s\n", config.channel_map_text);
    printf("channel_map_applied=%u\n", first->channel_map_applied ? 1 : 0);
    printf("data_set_patch=%s\n", config.data_set_patch_text);
    printf("data_set_patch_applied=%u\n", first->data_set_patch_applied ? 1 : 0);
    printf("joypad_script_ran=%u\n", first->joypad_script_ran ? 1 : 0);
    printf("start_pressed=%u\n", config.press_start ? 1 : 0);
    printf("ok=%u\n", (timed_out == 0 && all_boot_finished && match) ? 1 : 0);
    printf("timeout=%u\n", timed_out ? 1 : 0);
    printf("boot_finished=%u\n", boot_finished ? 1 : 0);
    printf("all_boot_finished=%u\n", all_boot_finished);
    printf("serial_ready=%u\n", serial_ready ? 1 : 0);
    printf("all_serial_ready=%u\n", all_serial_ready);
    printf("bytes_sent=%zu\n", timed_out ? 0 : byte_count);
    printf("sound_writes=%u\n", state->sound_writes);
    printf("sound_hash=%016llx\n", (unsigned long long)state->sound_hash);
    printf("serial_register_writes=%u\n", state->serial_register_writes);
    printf("trigger_pu1=%u\n", state->trigger_pu1);
    printf("trigger_pu2=%u\n", state->trigger_pu2);
    printf("trigger_wav=%u\n", state->trigger_wav);
    printf("trigger_noi=%u\n", state->trigger_noi);
    printf("audio_samples=%u\n", state->audio_samples);
    printf("audio_energy=%llu\n", (unsigned long long)state->audio_energy);
    printf("audio_hash=%016llx\n", (unsigned long long)state->audio_hash);
    printf("apu_hash=%016llx\n", (unsigned long long)apu_hash);
    printf("apu_hex=");
    for (uint16_t addr = 0xFF10; addr <= 0xFF3F; addr++) {
        printf("%02x", gb->io_registers[addr - 0xFF00]);
    }
    printf("\n");
    printf("nr51=%02x\n", nr51);
    printf("nr52=%02x\n", nr52);
    printf("nr12=%02x\n", nr12);
    printf("nr13=%02x\n", nr13);
    printf("nr14=%02x\n", nr14);
    printf("nr22=%02x\n", nr22);
    printf("nr32=%02x\n", nr32);
    printf("nr42=%02x\n", nr42);
    printf("panic_silenced=%u\n", (nr12 == 0 && nr22 == 0 && nr32 == 0 && nr42 == 0) ? 1 : 0);
    printf("logs=%u\n", all_logs);
    printf("log_hash=%016llx\n", (unsigned long long)state->log_hash);
    printf("pc=%04x\n", pc);
    printf("sp=%04x\n", sp);
    printf("sram_size=%zu\n", sram_size(gb));
    printf("sram_hash=%016llx\n", (unsigned long long)sram_hash(gb));
    printf("sram_magic=%02x\n", sram_byte(gb, 512));
    printf("sram_checksum=%02x\n", sram_byte(gb, 513));
    printf("sram_expected_checksum=%02x\n", mGB_save_checksum(GB_get_direct_access(gb, GB_DIRECT_ACCESS_CART_RAM, NULL, NULL), sram_size(gb)));
    printf("sram_valid=%u\n", sram_valid(gb));
    printf("save_hash=%016llx\n", (unsigned long long)save_hash(first));
    printf("save_magic=%02x\n", save_byte(first, 512));
    printf("save_checksum=%02x\n", save_byte(first, 513));
    printf("save_expected_checksum=%02x\n", mGB_save_checksum(save_data_ptr(first), save_data_ptr(first) ? 514 : 0));
    printf("save_valid=%u\n", save_valid(first));
    if (config.data_set_addr) {
        for (unsigned i = 0; i <= 38; i++) {
            printf("data_set_%u=%u\n", i, data_set_byte(first, i));
        }
    }
    print_instance_details(instances, config.instances);

    for (unsigned i = 0; i < config.instances; i++) {
        GB_free(instances[i].gb);
    }
    return timed_out ? 1 : 0;
}
