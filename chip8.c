#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "SDL.h"

#define SCREEN_WIDTH 64
#define SCREEN_HEIGHT 32

#define MIN(a, b) ((a) < (b) ? (a) : (b))

// SDL Object
typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
} sdl_t;

// Emulator configuration object
typedef struct {
    uint32_t fg_color;          // Foreground color RGBA (8|8|8|8 bits)
    uint32_t bg_color;          // Background color RGBA (8|8|8|8 bits)
    uint32_t scale_factor;      // Amount to scale CHIP8 pixel
    bool pixel_outlines;        // Draw pixel outlines
    uint16_t insts_per_second;  // Number of CHIP8 instructions to emulate per second
} config_t;

// Emulator states
typedef enum {
    QUIT,
    RUNNING,
    PAUSED
} emulator_state_t;

// CHIP8 Instruction format
// CHIP-8 has 35 opcodes, which are all two bytes long and stored big-endian.
typedef struct {
    uint16_t opcode;
    uint16_t NNN;     // 12 bit address
    uint8_t NN;       // 8 bit constant
    uint8_t N;        // 4 bit constanst
    uint8_t X;        // 4 bit register identifier
    uint8_t Y;        // 4 bit register identifier
} instruction_t;

// CHIP8 Machine object
typedef struct {
    emulator_state_t state;  // state of machine
    uint8_t ram[4096];
    bool display[64*32];     // Should have be in ram[0xF00 ~ 0xFFF]
    uint16_t stack[12];      // Subroutine stack. 12 levels of nesting, 48 Bytes max
    uint8_t SP;              // Stack Pointer
    uint8_t V[16];           // V0 to VF
    uint16_t I;              // Index register
    uint16_t PC;             // Program Counter
    uint8_t delay_timer;      // Count down at 60 hz when > 0
    uint8_t sound_timer;      // Count down at 60 hzr when > 0. A beeping sound is made when > 0
    bool keypad[16];         // Hexadecimal keypad 0x0 to 0xF
    instruction_t inst;      // Currently executing instruction
    const char *rom_name;    // Currently running rom
} chip8_t;

// Set up initial emulator configuration from passed in arguments
static bool set_config_from_args(config_t *config, const int argc, char **argv)
{
    // Set defaults
    config->fg_color = 0x32FF66FF;   // Green
    config->bg_color = 0x000000FF;   // Black
    config->scale_factor = 20;       // 64x32 scaled by 20 to 1280x640
    config->pixel_outlines = true;   // Draw pixel outlines
    config->insts_per_second = 500;  // Number of instructions of CHIP8 to emulate per second

    // Override defaults from passed in arguments
    for (int i = 1; i < argc; i++) {
        (void)argv[i];
        // !!!--- TODO
    }

    return true;
}

// Initialize SDL
static bool init_sdl(sdl_t *sdl, const config_t config)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        SDL_Log("Could not initialize SDL subsystems. %s\n", SDL_GetError());
        return false;
    }

    sdl->window = SDL_CreateWindow("CHIP8 Emulator", SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED,
                                   SCREEN_WIDTH * config.scale_factor,
                                   SCREEN_HEIGHT * config.scale_factor,
                                   SDL_WINDOW_BORDERLESS);
    if (!sdl->window) {
        SDL_Log("Could not create SDL window. %s\n", SDL_GetError());
        return false;
    }

    sdl->renderer = SDL_CreateRenderer(sdl->window, -1, SDL_RENDERER_ACCELERATED);
    if (!sdl->renderer) {
        SDL_Log("Could not create SDL renderer. %s\n", SDL_GetError());
        return false;
    }

    return true;
}

// Initialize CHIP8 machine
static bool init_chip8(chip8_t *chip8, const char *rom_name)
{
    const uint16_t entry_point = 0x200;          // Most programs begin at memory location 512 (0x200)

    /*
        There are sixteen characters that ROMs expected at a certain location so they can write
        characters to the screen, so we need to put those characters into memory.
        Each character sprite is five bytes.  The character F, for example,
        is 0xF0, 0x80, 0xF0, 0x80, 0x80. Take a look at the binary representation:

        11110000
        10000000
        11110000
        10000000
        10000000
    */

    const uint8_t font[] = {
        0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
        0x20, 0x60, 0x20, 0x20, 0x70, // 1
        0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
        0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
        0x90, 0x90, 0xF0, 0x10, 0x10, // 4
        0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
        0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
        0xF0, 0x10, 0x20, 0x40, 0x40, // 7
        0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
        0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
        0xF0, 0x90, 0xF0, 0x90, 0x90, // A
        0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
        0xF0, 0x80, 0x80, 0x80, 0xF0, // C
        0xE0, 0x90, 0x90, 0x90, 0xE0, // D
        0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
        0xF0, 0x80, 0xF0, 0x80, 0x80  // F
    };

    // Load font
    // Anywhere in the first 512 bytes (000–1FF) is fine. For some reason, it’s become popular to put it at 050–09F
    memcpy(&chip8->ram[0], font, sizeof(font));

    // Open ROM file
    FILE *rom = fopen(rom_name, "rb");
    if (!rom) {
        SDL_Log("Rom file %s is invalid or does not exist\n", rom_name);
        return false;
    }

    // Find ROM size
    fseek(rom, 0, SEEK_END);
    const size_t rom_size = ftell(rom);
    const size_t max_size = sizeof chip8->ram - entry_point;
    fseek(rom, 0, SEEK_SET);

    if (rom_size > max_size) {
        SDL_Log("Rom file %s is too big! Rom size: %zu, Max size allowed: %zu\n", rom_name, rom_size, max_size);
        return false;
    }

    if (fread(&chip8->ram[entry_point], rom_size, 1, rom) != 1) {
        // Load ROM into RAM
        SDL_Log("Could not read ROM file into CHIP8 memory\n");
        return false;
    }
    fclose(rom);

    // Set chip8 machine defaults
    chip8->state = RUNNING;   // Set default machine state to running
    chip8->PC = entry_point;  // Start PC at ROM entry point
    chip8->SP = 0;            // Top of stack is at 0
    chip8->rom_name = rom_name;

    return true;
}

// Handle user input
/*
    Keypad       Keyboard
    +-+-+-+-+    +-+-+-+-+
    |1|2|3|C|    |1|2|3|4|
    +-+-+-+-+    +-+-+-+-+
    |4|5|6|D|    |q|w|e|r|
    +-+-+-+-+ => +-+-+-+-+
    |7|8|9|E|    |a|s|d|f|
    +-+-+-+-+    +-+-+-+-+
    |A|0|B|F|    |z|x|c|v|
    +-+-+-+-+    +-+-+-+-+
*/
static void handle_input(chip8_t *chip8)
{
    SDL_Event event;

    while (SDL_PollEvent(&event)) {  // Poll until all events are handled
        switch (event.type) {
            case SDL_QUIT:
                // Exit window; End program
                chip8->state = QUIT;  // Will exit main emulator loop
                SDL_Log("Emulator exiting");
                return;

            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        // Exit window; End program when escape key is pressed
                        chip8->state = QUIT;  // Will exit main emulator loop
                        SDL_Log("Emulator exiting");
                        return;

                    case SDLK_SPACE:
                        if (chip8->state == RUNNING) {
                            chip8->state = PAUSED;
                            SDL_Log("Emulator paused");
                        } else {
                            chip8->state = RUNNING;
                            SDL_Log("Emulator resumed");
                        }
                        return;

                    case SDLK_1:
                        chip8->keypad[0x1] = true;
                        break;

                    case SDLK_2:
                        chip8->keypad[0x2] = true;
                        break;

                    case SDLK_3:
                        chip8->keypad[0x3] = true;
                        break;

                    case SDLK_4:
                        chip8->keypad[0xC] = true;
                        break;

                    case SDLK_q:
                        chip8->keypad[0x4] = true;
                        break;

                    case SDLK_w:
                        chip8->keypad[0x5] = true;
                        break;

                    case SDLK_e:
                        chip8->keypad[0x6] = true;
                        break;

                    case SDLK_r:
                        chip8->keypad[0xD] = true;
                        break;

                    case SDLK_a:
                        chip8->keypad[0x7] = true;
                        break;

                    case SDLK_s:
                        chip8->keypad[0x8] = true;
                        break;

                    case SDLK_d:
                        chip8->keypad[0x9] = true;
                        break;

                    case SDLK_f:
                        chip8->keypad[0xE] = true;
                        break;

                    case SDLK_z:
                        chip8->keypad[0xA] = true;
                        break;

                    case SDLK_x:
                        chip8->keypad[0x0] = true;
                        break;

                    case SDLK_c:
                        chip8->keypad[0xB] = true;
                        break;

                    case SDLK_v:
                        chip8->keypad[0xF] = true;
                        break;

                    default:
                        break;
                }
                break;

            case SDL_KEYUP:
                switch (event.key.keysym.sym) {
                    case SDLK_1:
                        chip8->keypad[0x1] = false;
                        break;

                    case SDLK_2:
                        chip8->keypad[0x2] = false;
                        break;

                    case SDLK_3:
                        chip8->keypad[0x3] = false;
                        break;

                    case SDLK_4:
                        chip8->keypad[0xC] = false;
                        break;

                    case SDLK_q:
                        chip8->keypad[0x4] = false;
                        break;

                    case SDLK_w:
                        chip8->keypad[0x5] = false;
                        break;

                    case SDLK_e:
                        chip8->keypad[0x6] = false;
                        break;

                    case SDLK_r:
                        chip8->keypad[0xD] = false;
                        break;

                    case SDLK_a:
                        chip8->keypad[0x7] = false;
                        break;

                    case SDLK_s:
                        chip8->keypad[0x8] = false;
                        break;

                    case SDLK_d:
                        chip8->keypad[0x9] = false;
                        break;

                    case SDLK_f:
                        chip8->keypad[0xE] = false;
                        break;

                    case SDLK_z:
                        chip8->keypad[0xA] = false;
                        break;

                    case SDLK_x:
                        chip8->keypad[0x0] = false;
                        break;

                    case SDLK_c:
                        chip8->keypad[0xB] = false;
                        break;

                    case SDLK_v:
                        chip8->keypad[0xF] = false;
                        break;

                    default:
                        break;
                }
                break;

            default:
                break;
        }
    }
}

// Clear screen (SDL Window) to background color
static void clear_screen(const sdl_t sdl, const config_t config)
{
    const uint8_t r = (config.bg_color >> 24) & 0xFF;  // red
    const uint8_t g = (config.bg_color >> 16) & 0xFF;  // green
    const uint8_t b = (config.bg_color >> 8) & 0xFF;   // blue
    const uint8_t a = (config.bg_color >> 0) & 0xFF;   // alpha
    SDL_SetRenderDrawColor(sdl.renderer, r, g, b, a);
    SDL_RenderClear(sdl.renderer);
}

// Update window with any changes
static void update_screen(const sdl_t sdl, const config_t config, const chip8_t chip8)
{
    SDL_Rect rect;
    rect.w = config.scale_factor;  // Scale it by scale factor
    rect.h = config.scale_factor;  // Scale it by scale factor

    // Get color values for foreground
    const uint8_t fg_r = (config.fg_color >> 24) & 0xFF;
    const uint8_t fg_g = (config.fg_color >> 16) & 0xFF;
    const uint8_t fg_b = (config.fg_color >> 8) & 0xFF;
    const uint8_t fg_a = (config.fg_color >> 0) & 0xFF;

    // Get color values for background
    const uint8_t bg_r = (config.bg_color >> 24) & 0xFF;
    const uint8_t bg_g = (config.bg_color >> 16) & 0xFF;
    const uint8_t bg_b = (config.bg_color >> 8) & 0xFF;
    const uint8_t bg_a = (config.bg_color >> 0) & 0xFF;

    // Read each display pixel, draw a rectangle per pixel to the SDL window
    for (uint8_t x = 0; x < SCREEN_WIDTH; x++) {
        for (uint8_t y = 0; y < SCREEN_HEIGHT; y++) {
            rect.x = x * config.scale_factor;
            rect.y = y * config.scale_factor;

            uint16_t index = y * SCREEN_WIDTH + x;
            if (chip8.display[index]) {
                // If pixel is on, set renderer to foreground color
                SDL_SetRenderDrawColor(sdl.renderer, fg_r, fg_g, fg_b, fg_a);
            } else {
                // If pixel is off, set renderer to background color
                SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
            }
            SDL_RenderFillRect(sdl.renderer, &rect);

            if (config.pixel_outlines) {
                // If pixel outline mode is enabled
                SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
                SDL_RenderDrawRect(sdl.renderer, &rect);
            }
        }
    }

    SDL_RenderPresent(sdl.renderer);
}

// Final cleanup
static void final_cleanup(const sdl_t sdl)
{
    SDL_DestroyRenderer(sdl.renderer);
    SDL_DestroyWindow(sdl.window);
    SDL_Quit();  // Shut down SDL subsystem
}

static void emulate_instruction(chip8_t *chip8)
{
    // CHIP-8 opcodes are all two bytes long and stored big-endian.
    // Get next opcode from RAM
    chip8->inst.opcode = (chip8->ram[chip8->PC] << 8 | chip8->ram[chip8->PC + 1]);
    chip8->PC += 2;  // Increment PC for next opcode

    // Fill out instruction format
    // Ex: 1NNN, 4XNN, 6XNN, DXYN
    chip8->inst.NNN = chip8->inst.opcode & 0xFFF;
    chip8->inst.NN = chip8->inst.opcode & 0xFF;
    chip8->inst.N = chip8->inst.opcode & 0xF;
    chip8->inst.X = (chip8->inst.opcode >> 8) & 0xF;
    chip8->inst.Y = (chip8->inst.opcode >> 4) & 0xF;

    // Emulate opcode
    switch ((chip8->inst.opcode >> 12) & 0xF) {
        case 0x0:
            if (chip8->inst.NN == 0xE0) {
                // 0x00E0: Clears the screen
                memset(&chip8->display[0], false, sizeof(chip8->display));  // RAM[0xF00 ~ 0xFFF] (256 Bytes)
            } else if (chip8->inst.NN == 0xEE) {
                // 0x00EE: Returns from a subroutine
                // With each RET, the stack pointer is decremented by
                // one and the address that it’s pointing to is put into
                // the PC for execution.
                chip8->SP -= 1;
                chip8->PC = chip8->stack[chip8->SP];
            } else {
                // Unimplemented for opcode 0xNNN.
                // Calls machine code routine (RCA 1802 for COSMAC VIP) at address NNN. Not necessary for most ROMs.
            }
            break;

        case 0x1:
            // 0x1NNN: Jumps to address NNN
            chip8->PC = chip8->inst.NNN;  // Set PC to NNN
            break;

        case 0x2:
            // 0x2NNN: Calls subroutine at NNN
            // With each CALL, the current PC (which was previously
            // incremented to point to the next instruction) is placed
            // where the SP was pointing, and the SP is incremented.
            chip8->stack[chip8->SP] = chip8->PC;
            chip8->PC = chip8->inst.NNN;
            chip8->SP += 1;
            break;

        case 0x3:
            // 0x3XNN: Skips the next instruction if VX equals NN
            if (chip8->V[chip8->inst.X] == chip8->inst.NN)
                chip8->PC += 2;
            break;

        case 0x4:
            // 0x4XNN: Skips the next instruction if VX does not equal NN
            if (chip8->V[chip8->inst.X] != chip8->inst.NN)
                chip8->PC += 2;
            break;

        case 0x5:
            // 0x5XY0: Skips the next instruction if VX equals VY
            if (chip8->V[chip8->inst.X] == chip8->V[chip8->inst.Y])
                chip8->PC += 2;
            break;

        case 0x6:
            // 0x6XNN: Sets VX to NN.
            chip8->V[chip8->inst.X] = chip8->inst.NN;
            break;

        case 0x7:
            // 0x7XNN: Vx += NN, Adds NN to VX (carry flag is not changed).
            chip8->V[chip8->inst.X] += chip8->inst.NN;
            break;

        case 0x8:
            switch (chip8->inst.N) {
                case 0x0:
                    // 0x8XY0: Sets VX to the value of VY.
                    chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y];
                    break;

                case 0x1:
                    // 0x8XY1: Sets VX |= VY
                    chip8->V[chip8->inst.X] |= chip8->V[chip8->inst.Y];
                    break;

                case 0x2:
                    // 0x8XY2: Sets VX &= VY
                    chip8->V[chip8->inst.X] &= chip8->V[chip8->inst.Y];
                    break;

                case 0x3:
                    // 0x8XY3: Sets VX ^= VY
                    chip8->V[chip8->inst.X] ^= chip8->V[chip8->inst.Y];
                    break;

                case 0x4:
                    // 0x8XY4: Adds VY to VX. VF is set to 1 when VX + VY > FF
                    uint16_t sum = chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y];
                    if (sum > 0xFF) {
                        chip8->V[0xF] = 1;
                    } else {
                        chip8->V[0xF] = 0;
                    }
                    chip8->V[chip8->inst.X] += chip8->V[chip8->inst.Y];
                    break;

                case 0x5:
                    // 0x8XY5: VY is subtracted from VX. VF = 00 if VX < VY, VF = 01 if VX >= VY
                    if (chip8->V[chip8->inst.X] >= chip8->V[chip8->inst.Y]) {
                        chip8->V[0xF] = 1;
                    } else {
                        chip8->V[0xF] = 0;
                    }
                    chip8->V[chip8->inst.X] -= chip8->V[chip8->inst.Y];
                    break;

                case 0x6:
                    // 0x8XY6: Stores the least significant bit of VX in VF and then shifts VX to the right by 1
                    chip8->V[0xF] = chip8->V[chip8->inst.X] & 0x1;
                    chip8->V[chip8->inst.X] >>= 1;
                    break;

                case 0x7:
                    // 0x8XY7: Sets VX to VY minus VX. VF = 00 if VX >= VY, VF = 01 if VX < VY
                    if (chip8->V[chip8->inst.Y] > chip8->V[chip8->inst.X]) {
                        chip8->V[0xF] = 1;
                    } else {
                        chip8->V[0xF] = 0;
                    }
                    chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X];
                    break;

                case 0xE:
                    // 0x8XYE: Stores the most significant bit of VX in VF and then shifts VX to the left by 1.
                    chip8->V[0xF] = (chip8->V[chip8->inst.X] & 0x80) >> 7;
                    chip8->V[chip8->inst.X] <<= 1;
                    break;

                default:
                    break;
            }
            break;

        case 0x9:
            // 0x9XY0: Skips the next instruction if VX does not equal VY
            if (chip8->V[chip8->inst.X] != chip8->V[chip8->inst.Y])
                chip8->PC += 2;
            break;

        case 0xA:
            // 0xANNN: Sets Index register I to the address NNN.
            chip8->I = chip8->inst.NNN;
            break;

        case 0xB:
            // 0xBNNN: Jumps to the address NNN plus V0.
            chip8->PC = chip8->V[0x0] + chip8->inst.NNN;
            break;

        case 0xC:
            // 0xCNNN: Sets VX to the result of a bitwise and operation on a random number (Typically: 0 to 255) and NN.
            chip8->V[chip8->inst.X] = (rand() % 256) & chip8->inst.NN;
            break;

        case 0xD:
            // 0xDXYN:
            /*
                Draws a sprite at coordinate (VX, VY) that has a width of 8 pixels and a height of N pixels.
                Each row of 8 pixels is read as bit-coded starting from memory location I;
                I value does not change after the execution of this instruction.
                As described above, VF is set to 1 if any screen pixels are flipped from set to unset
                when the sprite is drawn, and to 0 if that does not happen.

                The starting position of the sprite will wrap. In other words, an X coordinate of 4 is the same
                as an X of 68 (since the screen is 64 pixels wide).

                The actual drawing of the sprite should not wrap. If a sprite is drawn near the edge of the
                screen, it should be clipped, and not wrap. The sprite should be partly drawn near the edge,
                and the other part should not reappear on the opposite side of the screen.
            */

            const uint8_t x_start = chip8->V[chip8->inst.X] % SCREEN_WIDTH;
            const uint8_t y_start = chip8->V[chip8->inst.Y] % SCREEN_HEIGHT;

            const uint8_t x_end = MIN(x_start + 8, SCREEN_WIDTH);
            const uint8_t y_end = MIN(y_start + chip8->inst.N, SCREEN_HEIGHT);

            chip8->V[0xF] = 0;  // Set Carry flag to 0

            for (int y = y_start; y < y_end; y++) {
                const uint8_t sprite_byte = chip8->ram[chip8->I + (y - y_start)];

                for (int x = x_start; x < x_end; x++) {
                    // Get pixels from left to right
                    uint16_t index = y * SCREEN_WIDTH + x;
                    bool *pixel = &chip8->display[index];
                    const bool sprite_bit = sprite_byte & (0x80 >> (x - x_start));

                    // XOR display pixel with sprite pixel to set it on or off
                    *pixel ^= sprite_bit;
                }
            }

            break;

        case 0xE:
            if (chip8->inst.NN == 0x9E) {
                // 0xEX9E: Skips the next instruction if the key stored in VX is pressed
                uint8_t VX = chip8->V[chip8->inst.X];
                if (chip8->keypad[VX])
                    chip8->PC += 2;
            } else if (chip8->inst.NN == 0xA1) {
                // 0xEXA1: Skips the next instruction if the key stored in VX is not pressed
                uint8_t VX = chip8->V[chip8->inst.X];
                if (!chip8->keypad[VX])
                chip8->PC += 2;
            }
            break;

        case 0xF:
            switch (chip8->inst.NN) {
                case 0x07:
                    // 0xFX07: Sets VX to the value of the delay timer
                    chip8->V[chip8->inst.X] = chip8->delay_timer;
                    break;

                case 0x0A:
                    // 0xFX0A: A key press is awaited, and then stored in VX
                    bool key_pressed = false;
                    for (uint8_t key = 0; key < 0xF; key++) {
                        if (chip8->keypad[key]) {
                            chip8->V[chip8->inst.X] = key;
                            key_pressed = true;
                            break;
                        }
                    }
                    // The easiest way to “wait” is to decrement the PC by 2
                    // whenever a keypad value is not detected.
                    if (!key_pressed)
                        chip8->PC -= 2;
                    break;

                case 0x15:
                    // 0xFX15: Sets the delay timer to VX
                    chip8->delay_timer = chip8->V[chip8->inst.X];
                    break;

                case 0x18:
                    // 0xFX18: Sets the sound timer to VX
                    chip8->sound_timer = chip8->V[chip8->inst.X];
                    break;

                case 0x1E:
                    // 0xFX1E: Adds VX to I. VF is not affected
                    chip8->I += chip8->V[chip8->inst.X];
                    break;

                case 0x29:
                    // 0xFX29: Sets I to the location of the sprite for the character in VX
                    chip8->I = 0 + chip8->V[chip8->inst.X] * 5;  // Fonts loaded at address 0 in RAM
                    break;

                case 0x33:
                    // 0xFX33: Stores the binary-coded decimal representation of VX,
                    //         with the hundreds digit in memory at location in I,
                    //         the tens digit at location I+1, and the ones digit at location I+2
                    uint8_t bcd = chip8->V[chip8->inst.X];
                    chip8->ram[chip8->I + 2] = bcd % 10;  // Ones place
                    bcd /= 10;
                    chip8->ram[chip8->I + 1] = bcd % 10;  // Tens place
                    bcd /= 10;
                    chip8->ram[chip8->I] = bcd % 10;      // Hundreds place
                    break;

                case 0x55:
                    // 0xFX55: Stores from V0 to VX (including VX) in memory, starting at address I.
                    //         The offset from I is increased by 1 for each value written,
                    //         but I itself is left unmodified. SCHIP does not increment I, CHIP8 does increment I
                    for (uint8_t i = 0; i <= chip8->inst.X; i++) {
                        chip8->ram[chip8->I + i] = chip8->V[i];
                    }
                    break;

                case 0x65:
                    // 0xFX65: Fills from V0 to VX (including VX) with values from memory, starting at address I.
                    //         The offset from I is increased by 1 for each value read, but I itself is left unmodified.
                    //         In the original CHIP-8 implementation, and also in CHIP-48, I is left incremented after
                    //         this instruction had been executed. In SCHIP, I is left unmodified.
                    for (uint8_t i = 0; i <= chip8->inst.X; i++) {
                        chip8->V[i] = chip8->ram[chip8->I + i];
                    }
                    break;
            }
            break;

        default:
            break;
    }
}

// Update CHIP8 delay and sound times every 60 hz
void update_timers(chip8_t *chip8)
{
    /*
        They both count down at 60 hertz, until they reach 0.
        Delay timer: This timer is intended to be used for timing the events of games. Its value can be set and read.
        Sound timer: This timer is used for sound effects. When its value is nonzero, a beeping sound is made.
    */

    if (chip8->delay_timer > 0)
        chip8->delay_timer--;

    if (chip8->sound_timer > 0) {
        chip8->sound_timer--;
        // TODO: Play sound
    } else {
        // TODO: Stop playing sound
    }
}

int main(int argc, char **argv)
{
    // Usage message
    if (argc == 1) {
        fprintf(stderr, "Usage: %s <rom-name>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    // Initialize emulator configurations
    config_t config = {0};
    if (!set_config_from_args(&config, argc, argv))
        exit(EXIT_FAILURE);

    // Initialize SDL
    sdl_t sdl = {0};
    if (!init_sdl(&sdl, config))
        exit(EXIT_FAILURE);

    // Initialize CHIP8 machine
    chip8_t chip8 = {0};
    const char *rom_name = argv[1];
    if (!init_chip8(&chip8, rom_name))
        exit(EXIT_FAILURE);

    // Initial screen clear to background color
    clear_screen(sdl, config);

    // Seed the random number generator with the current time
    srand(time(NULL));

    // Main emulator loop
    // Each frame starts here, one loop is one frame
    while (chip8.state != QUIT) {

        // Get time before running instructions & input
        const uint64_t start_frame_time = SDL_GetPerformanceCounter();

        // Handle user input
        handle_input(&chip8);

        if (chip8.state == PAUSED)
            continue;

        // Emulate CHIP8 Instructions per frame (60 hz)
        for (uint16_t i = 0; i < config.insts_per_second / 60; i++)
            emulate_instruction(&chip8);

        // Get time after running instructions & handling input
        const uint64_t end_frame_time = SDL_GetPerformanceCounter();

        // Time elapsed between end frame & start frame in milliseconds (ms)
        const double time_elapsed = (double)((end_frame_time - start_frame_time) * 1000) / SDL_GetPerformanceFrequency();

        // Delay such that each frame takes 16.67 ms (60 hz)
        double target_frame_time = 16.67;  // Target frame time in milliseconds
        uint32_t delay_time = (uint32_t)(target_frame_time > time_elapsed ? target_frame_time - time_elapsed : 0);
        SDL_Delay(delay_time);

        // Update window with changes for this frame. Updates every 60 hz
        update_screen(sdl, config, chip8);
        // Update delay & sound times every 60 hz, i.e, end of each frame
        update_timers(&chip8);
    }

    // Final cleanup
    final_cleanup(sdl);

    exit(EXIT_SUCCESS);
}