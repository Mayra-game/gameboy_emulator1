# Game Boy Emulator (C)

A small, dependency-free Game Boy (DMG) emulator written in C11. Its organization follows the interpreter-first approach in [Cinoop](https://cturt.github.io/cinoop.html): CPU instruction execution, memory and cartridge mapping, then graphics and audio.

## Build and run

Apple Clang or another C11 compiler is required. From this folder:

```sh
make
make test
./gameboy_emulator path/to/game.gb
```

The emulator runs at most one million instructions by default. Choose a limit, save the framebuffer to a 160×144 PPM image, or capture audio to a 44.1 kHz stereo WAV file:

```sh
./gameboy_emulator --steps 5000000 --frame frame.ppm path/to/game.gb
./gameboy_emulator --steps 5000000 --audio audio.wav path/to/game.gb
./gameboy_emulator --play path/to/game.gb
```

The entry point assumes the post-boot DMG CPU register state (`PC=0x0100`); it does not execute the Nintendo boot ROM. `--self-test` runs CPU, memory, cartridge, PPU, input, and APU checks. `make run ROM=path/to/game.gb` is also available.

## Implemented

- LR35902 register pairs, stack, arithmetic/logic flags, relative/absolute jumps, calls, returns, restart vectors, interrupt entry, HALT/STOP, and instruction cycle accounting.
- Algorithmic decoding for the regular base-opcode families and all CB-prefixed rotate/shift, bit, reset, and set operations; illegal opcodes are reported.
- 64 KiB address space, echo RAM, unusable OAM gap, I/O registers, interrupt request/enable registers, active-low joypad state, cycle-driven DIV/TIMA timer, and joypad interrupt signaling.
- ROM-only and MBC1 ROM/RAM banking for cartridge images up to 2 MiB.
- Cycle-driven DMG LCD modes, LY/LYC and STAT behavior, VBlank/STAT interrupts, background and window tile rendering, 8×8/8×16 sprites with palette and priority handling, and OAM DMA.
- A basic APU with pulse, wave, and noise channel output mixed to optional stereo PCM WAV capture.
- Optional real-time terminal play mode with a scaled color display and keyboard input.
- A PPM snapshot of the emulated framebuffer after the selected instruction budget.

## Limitations

`--play` runs until you press `Q` in an interactive terminal. Controls are **W/A/S/D** for directions, **J** for A, **K** for B, **U** for Select, and **I** for Start. It renders a reduced 80-column display and briefly holds each keypress to represent a button press.

This is an educational DMG emulator, not a cycle-accurate or full commercial-game compatibility claim. There is no desktop window frontend; `--play` uses an ANSI terminal. Joypad state is also available through the `memory_set_button` API. APU sweep and exact frame sequencing/mixing are simplified, and WAV capture is offline (there is no live playback). The Nintendo boot ROM, serial link, battery-backed save persistence, and MBC2/3/5 or other mapper support are absent. DMA copies OAM immediately rather than modeling bus stalls, timer reload timing is simplified, and some interrupt/HALT edge cases remain approximate. Some ROMs will not boot or behave correctly under these limits.

The project is an educational implementation informed by Cinoop's design discussion; it does not copy Cinoop source code.
