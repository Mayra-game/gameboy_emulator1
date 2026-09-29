#ifndef GB_MEMORY_H
#define GB_MEMORY_H
#include <stddef.h>
#include <stdint.h>
#define GB_MEM_SIZE 65536
#define GB_ROM_MAX (2u * 1024u * 1024u)
typedef struct {
    uint8_t data[GB_MEM_SIZE];
    uint8_t rom[GB_ROM_MAX];
    uint8_t eram[4 * 0x2000];
    uint8_t framebuffer[160 * 144];
    size_t rom_size;
    unsigned rom_banks, ram_banks, rom_bank, ram_bank;
    uint8_t cartridge_type, banking_mode, ram_enabled;
    uint8_t joypad, ime, interrupt_delay;
    uint16_t div_counter;
    uint8_t timer_reload_pending;
    uint16_t ppu_line_cycles;
    uint32_t ppu_frames;
    uint8_t window_line, stat_irq_line, dma_active;
    void *audio_file;
    uint32_t audio_sample_phase, audio_elapsed_cycles, audio_frame_cycles;
    uint32_t audio_phase[3], audio_envelope_cycles[3], noise_cycles;
    uint16_t audio_length[4], noise_lfsr;
    uint8_t audio_active, audio_volume[3], audio_envelope_timer[3];
    uint32_t audio_samples;
} Memory;
void memory_init(Memory *m);
void memory_init_post_boot(Memory *m);
int memory_load_rom(Memory *m, const char *path, char *error, size_t error_size);
uint8_t memory_read(Memory *m, uint16_t address);
void memory_write(Memory *m, uint16_t address, uint8_t value);
void memory_tick(Memory *m, unsigned cycles);
void memory_request_interrupt(Memory *m, uint8_t bit);
void memory_set_button(Memory *m, unsigned button, int pressed);
int memory_audio_open(Memory *m, const char *path);
int memory_audio_close(Memory *m);
#endif
