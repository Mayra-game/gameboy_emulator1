#include "cpu.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

#define WIDTH 160
#define HEIGHT 144
static uint64_t now_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000u + (uint64_t)tv.tv_usec;
}
static int terminal_begin(struct termios *saved) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
        return 0;
    if (tcgetattr(STDIN_FILENO, saved) != 0)
        return 0;
    struct termios raw = *saved;
    raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
    raw.c_iflag &= (tcflag_t) ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
        return 0;
    write(STDOUT_FILENO, "\033[?25l\033[2J", 10);
    return 1;
}
static void terminal_end(const struct termios *saved) {
    static const char restore[] = "\033[0m\033[?25h\033[2J\033[H";
    tcsetattr(STDIN_FILENO, TCSANOW, saved);
    write(STDOUT_FILENO, restore, sizeof restore - 1);
}
static void terminal_keys(Memory *m, uint64_t until[8], int *quit) {
    static const unsigned buttons[] = {0, 1, 2, 3, 4, 5, 6, 7};
    uint64_t now = now_us();
    for (unsigned i = 0; i < 8; i++)
        if (until[i] && now >= until[i]) {
            memory_set_button(m, i, 0);
            until[i] = 0;
        }
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval zero = {0, 0};
    while (select(STDIN_FILENO + 1, &fds, NULL, NULL, &zero) > 0) {
        unsigned char c;
        if (read(STDIN_FILENO, &c, 1) != 1)
            break;
        if (c == 'q' || c == 'Q' || c == 3) {
            *quit = 1;
            break;
        }
        int key = -1;
        switch (c) {
        case 'd':
        case 'D':
            key = 0;
            break;
        case 'a':
        case 'A':
            key = 1;
            break;
        case 'w':
        case 'W':
            key = 2;
            break;
        case 's':
        case 'S':
            key = 3;
            break;
        case 'j':
        case 'J':
            key = 4;
            break;
        case 'k':
        case 'K':
            key = 5;
            break;
        case 'u':
        case 'U':
            key = 6;
            break;
        case 'i':
        case 'I':
            key = 7;
            break;
        default:
            break;
        }
        if (key >= 0) {
            memory_set_button(m, buttons[key], 1);
            until[buttons[key]] = now + 140000;
        }
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
    }
}
static void terminal_frame(const Memory *m) {
    static const unsigned char colors[4][3] = {
        {224, 248, 208}, {136, 192, 112}, {52, 104, 86}, {8, 24, 32}};
    char output[100000];
    size_t n = 0;
    n += (size_t)snprintf(output + n, sizeof output - n, "\033[H");
    for (unsigned row = 0; row < 23; row++) {
        unsigned y0 = (row * 2 * HEIGHT) / (23 * 2), y1 = ((row * 2 + 1) * HEIGHT) / (23 * 2);
        for (unsigned col = 0; col < 80; col++) {
            unsigned x = col * 2 + 1;
            const unsigned char *fg = colors[m->framebuffer[y0 * 160 + x] & 3],
                                *bg = colors[m->framebuffer[y1 * 160 + x] & 3];
            n += (size_t)snprintf(output + n, sizeof output - n,
                                  "\033[38;2;%u;%u;%um\033[48;2;%u;%u;%um▀", fg[0], fg[1], fg[2],
                                  bg[0], bg[1], bg[2]);
        }
        n += (size_t)snprintf(output + n, sizeof output - n, "\033[0m\r\n");
    }
    n += (size_t)snprintf(output + n, sizeof output - n,
                          "WASD move  J A  K B  U Select  I Start  Q Quit\033[K");
    size_t sent = 0;
    while (sent < n) {
        ssize_t wrote = write(STDOUT_FILENO, output + sent, n - sent);
        if (wrote <= 0)
            break;
        sent += (size_t)wrote;
    }
}
static int frame_ppm(Memory *m, const char *path) {
    static const unsigned char colors[4][3] = {
        {224, 248, 208}, {136, 192, 112}, {52, 104, 86}, {8, 24, 32}};
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        return 0;
    }
    fprintf(f, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        unsigned shade = m->framebuffer[i] & 3;
        if (fwrite(colors[shade], 1, 3, f) != 3) {
            fclose(f);
            return 0;
        }
    }
    return fclose(f) == 0;
}
static int self_test(void) {
    Memory m;
    CPU c;
    memory_init(&m);
    memory_init_post_boot(&m);
    if (memory_read(&m, 0xFF04) != 0xAB || memory_read(&m, 0xFF40) != 0x91 ||
        (memory_read(&m, 0xFF41) & 3) != 2 || memory_read(&m, 0xFF47) != 0xFC) {
        fprintf(stderr, "post-boot hardware register self-test failed\n");
        return 1;
    }

    /* Each CPU test starts from a clean memory map and a deliberately chosen program. */
    memory_init(&m);
    cpu_init(&c, 0);
    c.pc = 0x100;
    const uint8_t program[] = {0x3E, 0x0F, 0x06, 0x01, 0x80, 0xEA, 0x00, 0xC0, 0xCD,
                               0x10, 0x01, 0x76, 0x00, 0x00, 0x00, 0x00, 0x3C, 0xC9};
    memcpy(&m.data[0x100], program, sizeof program);
    m.data[0xFFFF] = 0;
    for (int i = 0; i < 16 && !c.halted; i++)
        cpu_step(&c, &m);
    if (m.data[0xC000] != 0x10 || c.a != 0x11 || !c.halted) {
        fprintf(stderr, "self-test failed: A=%02X [C000]=%02X PC=%04X halted=%u\n", c.a,
                m.data[0xC000], c.pc, c.halted);
        return 1;
    }
    memory_init(&m);
    cpu_init(&c, 0);
    c.pc = 0x100;
    const uint8_t ldhprog[] = {0x3E, 0x12, 0xE0, 0x80, 0x3E, 0x00, 0xF0, 0x80, 0x76};
    memcpy(&m.data[0x100], ldhprog, sizeof ldhprog);
    for (int i = 0; i < 6 && !c.halted; i++)
        cpu_step(&c, &m);
    if (m.data[0xFF80] != 0x12 || c.a != 0x12 || !c.halted) {
        fprintf(stderr, "LDH instruction self-test failed\n");
        return 1;
    }
    memory_init(&m);
    cpu_init(&c, 0);
    c.pc = 0x100;
    m.data[0xFFFF] = 1;
    m.data[0x0100] = 0x76;
    m.data[0x0101] = 0x3E;
    m.data[0x0102] = 0x42;
    cpu_step(&c, &m);
    cpu_step(&c, &m);
    if (c.halted || c.halt_bug || c.a != 0x3E || c.pc != 0x0102) {
        fprintf(stderr, "HALT bug self-test failed: A=%02X PC=%04X halted=%u bug=%u\n", c.a, c.pc,
                c.halted, c.halt_bug);
        return 1;
    }
    memory_init(&m);
    cpu_init(&c, 0);
    c.pc = 0x100;
    const uint8_t cbprog[] = {0x06, 0x80, 0xCB, 0x00, 0xCB, 0x78, 0xCB, 0xC0, 0xCB, 0x80, 0x76};
    memcpy(&m.data[0x100], cbprog, sizeof cbprog);
    for (int i = 0; i < 8 && !c.halted; i++)
        cpu_step(&c, &m);
    if (c.b != 0 || c.f != 0xB0 || !c.halted) {
        fprintf(stderr, "CB self-test failed: B=%02X F=%02X PC=%04X\n", c.b, c.f, c.pc);
        return 1;
    }
    memory_init(&m);
    m.rom_size = 0x8000;
    m.rom_banks = 2;
    m.cartridge_type = 1;
    m.ram_banks = 4;
    memory_write(&m, 0x0000, 0x0A);
    memory_write(&m, 0x6000, 1);
    memory_write(&m, 0x4000, 3);
    memory_write(&m, 0xA000, 0x5A);
    if (memory_read(&m, 0xA000) != 0x5A) {
        fprintf(stderr, "MBC1 external RAM bank self-test failed\n");
        return 1;
    }
    memory_init(&m);
    memory_write(&m, 0x8000, 0);
    memory_write(&m, 0x8001, 0xFF);
    memory_write(&m, 0xFF47, 0xE4);
    memory_write(&m, 0xFF40, 0x91);
    memory_tick(&m, 252);
    if (m.framebuffer[0] != 2 || m.framebuffer[159] != 2 || ((m.data[0xFF41] & 3) != 0)) {
        fprintf(stderr, "PPU scanline render self-test failed: pixels=%u,%u STAT=%02X\n",
                m.framebuffer[0], m.framebuffer[159], m.data[0xFF41]);
        return 1;
    }
    memory_write(&m, 0xFF40, 0);
    memory_write(&m, 0x8000, 0xFF);
    memory_write(&m, 0x8001, 0);
    memory_write(&m, 0xFE00, 16);
    memory_write(&m, 0xFE01, 8);
    memory_write(&m, 0xFE02, 0);
    memory_write(&m, 0xFE03, 0);
    memory_write(&m, 0xFF48, 0xE4);
    memory_write(&m, 0xFF40, 0x93);
    memory_tick(&m, 252);
    if (m.framebuffer[0] != 1) {
        fprintf(stderr, "PPU sprite render self-test failed: pixel=%u\n", m.framebuffer[0]);
        return 1;
    }
    memory_write(&m, 0xFF40, 0);
    memory_write(&m, 0x8020, 0xFF);
    memory_write(&m, 0x8021, 0xFF);
    memory_write(&m, 0x9C00, 2);
    memory_write(&m, 0xFF4A, 0);
    memory_write(&m, 0xFF4B, 7);
    memory_write(&m, 0xFF40, 0xF1);
    memory_tick(&m, 252);
    if (m.framebuffer[0] != 3) {
        fprintf(stderr, "PPU window render self-test failed: pixel=%u\n", m.framebuffer[0]);
        return 1;
    }
    memory_tick(&m, 144u * 456u);
    if (m.data[0xFF44] != 144 || (m.data[0xFF0F] & 1) == 0 || ((m.data[0xFF41] & 3) != 1)) {
        fprintf(stderr, "PPU VBlank self-test failed: LY=%u IF=%02X STAT=%02X\n", m.data[0xFF44],
                m.data[0xFF0F], m.data[0xFF41]);
        return 1;
    }
    memory_init(&m);
    for (unsigned i = 0; i < 0xA0; i++)
        m.data[0xC000 + i] = (uint8_t)(i ^ 0xA5);
    memory_write(&m, 0xFF46, 0xC0);
    if (m.data[0xFE00] != 0xA5 || m.data[0xFE9F] != (uint8_t)(0x9F ^ 0xA5)) {
        fprintf(stderr, "OAM DMA self-test failed\n");
        return 1;
    }
    memory_write(&m, 0xFF00, 0x10);
    memory_set_button(&m, 4, 1);
    if ((memory_read(&m, 0xFF00) & 1) || !(m.data[0xFF0F] & 0x10)) {
        fprintf(stderr, "joypad self-test failed\n");
        return 1;
    }
    memory_init(&m);
    memory_write(&m, 0xFF26, 0x80);
    memory_write(&m, 0xFF12, 0xF3);
    memory_write(&m, 0xFF14, 0x80);
    if (!(memory_read(&m, 0xFF26) & 1)) {
        fprintf(stderr, "APU channel trigger self-test failed\n");
        return 1;
    }
    memory_write(&m, 0xFF26, 0);
    if (memory_read(&m, 0xFF26) & 0x8F) {
        fprintf(stderr, "APU power-off self-test failed\n");
        return 1;
    }
    puts("Smoke tests passed (CPU, post-boot registers, LDH, HALT bug, CB opcodes, MBC1 RAM, LCD, "
         "DMA, joypad, APU controls).");
    return 0;
}
static void usage(const char *name) {
    printf("Usage: %s [--steps N] [--frame out.ppm] [--audio out.wav] [--play] "
           "ROM.gb\n       %s --self-test\n",
           name, name);
}
int main(int argc, char **argv) {
    const char *rom = NULL, *out = NULL, *audio = NULL;
    unsigned long steps = 1000000;
    int playing = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(argv[0]);
            return 0;
        } else if (!strcmp(argv[i], "--self-test"))
            return self_test();
        else if (!strcmp(argv[i], "--play"))
            playing = 1;
        else if (!strcmp(argv[i], "--steps") && i + 1 < argc) {
            char *end;
            errno = 0;
            steps = strtoul(argv[++i], &end, 10);
            if (errno || *end) {
                fprintf(stderr, "invalid step count\n");
                return 2;
            }
        } else if (!strcmp(argv[i], "--frame") && i + 1 < argc)
            out = argv[++i];
        else if (!strcmp(argv[i], "--audio") && i + 1 < argc)
            audio = argv[++i];
        else if (argv[i][0] == '-') {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 2;
        } else if (!rom)
            rom = argv[i];
        else {
            fprintf(stderr, "unexpected argument: %s\n", argv[i]);
            return 2;
        }
    }
    if (!rom) {
        usage(argv[0]);
        return 2;
    }
    Memory *m = malloc(sizeof *m);
    CPU c;
    if (!m) {
        perror("allocate memory");
        return 1;
    }
    memory_init(m);
    char error[256];
    if (!memory_load_rom(m, rom, error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        free(m);
        return 1;
    }
    if (m->cartridge_type != 0 && m->cartridge_type != 1 && m->cartridge_type != 2 &&
        m->cartridge_type != 3)
        fprintf(stderr,
                "warning: cartridge type 0x%02X is not supported; treating ROM as fixed mapping\n",
                m->cartridge_type);
    memory_init_post_boot(m);
    cpu_init(&c, 1);
    if (audio && !memory_audio_open(m, audio)) {
        free(m);
        return 1;
    }
    struct termios saved;
    int terminal_ready = 0, quit = 0;
    uint64_t key_until[8] = {0}, next_poll = 0, shown_frame = 0, next_pace = 70224,
             deadline = now_us();
    if (playing) {
        if (!terminal_begin(&saved)) {
            fprintf(stderr, "--play needs an interactive terminal on stdin and stdout\n");
            if (audio)
                memory_audio_close(m);
            free(m);
            return 1;
        }
        terminal_ready = 1;
    }
    for (unsigned long i = 0; playing ? !quit : i < steps && !c.halted; i++) {
        cpu_step(&c, m);
        if (playing && c.cycles >= next_poll) {
            terminal_keys(m, key_until, &quit);
            next_poll = c.cycles + 2048;
        }
        if (playing && c.cycles >= next_pace) {
            next_pace += 70224;
            if (m->ppu_frames != shown_frame) {
                shown_frame = m->ppu_frames;
                terminal_frame(m);
            }
            deadline += 16674;
            uint64_t now = now_us();
            if (deadline < now)
                deadline = now;
            else {
                uint64_t delay = deadline - now;
                struct timeval tv = {(time_t)(delay / 1000000), (suseconds_t)(delay % 1000000)};
                select(0, NULL, NULL, NULL, &tv);
            }
        }
    }
    if (terminal_ready) {
        for (unsigned i = 0; i < 8; i++)
            memory_set_button(m, i, 0);
        terminal_end(&saved);
    }
    if (audio) {
        if (!memory_audio_close(m)) {
            fprintf(stderr, "error finalizing WAV capture\n");
            free(m);
            return 1;
        }
        printf("Audio written to %s (%u samples at 44.1 kHz stereo)\n", audio, m->audio_samples);
    }
    printf("ROM: %s (%zu bytes), cartridge 0x%02X\nCPU stopped after %llu instructions / %llu "
           "cycles at PC=%04X\n",
           rom, m->rom_size, m->cartridge_type, (unsigned long long)c.instructions,
           (unsigned long long)c.cycles, c.pc);
    if (out && !frame_ppm(m, out)) {
        fprintf(stderr, "could not write framebuffer to %s\n", out);
        free(m);
        return 1;
    }
    if (out)
        printf("Framebuffer written to %s\n", out);
    free(m);
    return 0;
}
