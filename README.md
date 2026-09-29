# My Game Boy Emulator

This is a small Game Boy emulator written in C. It is a learning project about how a game console uses its CPU, memory, and other parts. The project follows ideas from the [Cinoop guide](https://cturt.github.io/cinoop.html).

## Build and run

You need a C compiler. On a Mac, Apple Clang is already available if Xcode Command Line Tools are installed.

Open Terminal in this project folder and run:

```sh
make
make test
```

To try a Game Boy game, you need your own legally obtained ROM file. Replace `my-game.gb` with the path to that file:

```sh
./gameboy_emulator my-game.gb
```

The normal run stops after one million instructions. You can choose a different limit, save a picture of the screen, or save sound to a WAV file:

```sh
./gameboy_emulator --steps 5000000 --frame screen.ppm my-game.gb
./gameboy_emulator --steps 5000000 --audio sound.wav my-game.gb
./gameboy_emulator --play my-game.gb
```

In play mode, use **W/A/S/D** to move, **J** for A, **K** for B, **U** for Select, and **I** for Start. Press **Q** to quit.

## How the main files work

- `src/main.c` starts the program, opens the game file, and connects the parts together.
- `src/cpu.c` reads and runs the Game Boy's instructions. It keeps track of the CPU registers and flags.
- `src/memory.c` handles requests to read or write memory. Depending on the address, it sends the request to game data, RAM, the screen, or another device.

The Game Boy CPU uses 16-bit addresses, so it can address 65,536 memory locations. For example, an instruction can ask to read the address stored in the `HL` registers. The CPU asks the memory code for that value, and the memory code finds the right place to read it from.

## What it can do

The emulator can run many common Game Boy CPU instructions, use basic game cartridges, update a simple Game Boy screen, and read button presses. It also has a basic sound system. The `make test` command runs checks for the CPU and several other parts of the emulator.

## What is not finished

This is a learning project, so it does not run every Game Boy game correctly. It starts with settings that are similar to the Game Boy after its startup screen, instead of running the original Nintendo boot program. Some timing and sound details are simplified, and it does not support every type of game cartridge. Play mode uses the Terminal rather than a separate game window.

The project is based on ideas in the Cinoop guide. It does not include the original Game Boy boot program or copy Cinoop's source code.
