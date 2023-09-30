#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "SDL.h"

// SDL Object
typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
} sdl_t;

// Emulator configuration object
typedef struct {
    uint32_t window_width;   // SDL window width
    uint32_t window_height;  // SDL window height
    uint32_t fg_color;       // Foreground color RGBA (8|8|8|8 bits)
    uint32_t bg_color;       // Background color RGBA (8|8|8|8 bits)
    uint32_t scale_factor;   // Amount to scale CHIP8 pixel
} config_t;

// Emulator states
typedef enum {
    QUIT,
    RUNNING,
    PAUSED
} emulator_state_t;

// CHIP8 Machine object
typedef struct {
    emulator_state_t state;
} chip8_t;

// Set up initial emulator configuration from passed in arguments
static bool set_config_from_args(config_t *config, const int argc, char **argv)
{
    // Set defaults
    config->window_width = 64;      // CHIP8 original width resolution
    config->window_height = 32;     // CHIP8 original height resolution
    config->fg_color = 0xFFFFFFFF;  // White
    config->bg_color = 0xFFFF00FF;  // Yellow
    config->scale_factor = 20;      // 64x32 scaled by 20 to 1280x640

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
                                   config.window_width * config.scale_factor,
                                   config.window_height * config.scale_factor,
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
static bool init_chip8(chip8_t *chip8)
{
    chip8->state = RUNNING;  // Set default machine state to running
    return true;
}

// Handle user input
static void handle_input(chip8_t *chip8)
{
    SDL_Event event;

    while (SDL_PollEvent(&event)) {  // Poll until all events are handled
        switch (event.type) {
            case SDL_QUIT:
                // Exit window; End program
                chip8->state = QUIT;  // Will exit main emulator loop
                SDL_Log("Quiting");
                return;
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        // Exit window; End program when escape key is pressed
                        chip8->state = QUIT;  // Will exit main emulator loop
                        SDL_Log("Quiting");
                        return;
                    default:
                        break;
                }
                break;
            case SDL_KEYUP:
                break;
            default:
                break;
        }
    }
}

// Clear screen (SDL Window) to background color
static void clear_screen(const sdl_t sdl, const config_t config)
{
    const uint8_t r = (config.bg_color >> 24) & 0xFF;
    const uint8_t g = (config.bg_color >> 16) & 0xFF;
    const uint8_t b = (config.bg_color >> 8) & 0xFF;
    const uint8_t a = (config.bg_color >> 0) & 0xFF;
    SDL_SetRenderDrawColor(sdl.renderer, r, g, b, a);
    SDL_RenderClear(sdl.renderer);
}

// Update window with any changes
static void update_screen(const sdl_t sdl)
{
    // !!!!!!!!-- IMPORTANT
    SDL_RenderClear(sdl.renderer);  // DONT KNOW IF THIS LINE IS NEEDED, AFFECTS PERFORMANCE
    // !!!!!!!!-- IMPORTANT
    SDL_RenderPresent(sdl.renderer);
}

// Final cleanup
static void final_cleanup(const sdl_t sdl)
{
    SDL_DestroyRenderer(sdl.renderer);
    SDL_DestroyWindow(sdl.window);
    SDL_Quit();  // Shut down SDL subsystem
}

int main(int argc, char **argv)
{
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
    if (!init_chip8(&chip8))
        exit(EXIT_FAILURE);

    // Initial screen clear to background color
    clear_screen(sdl, config);

    // Main emulator loop
    while (chip8.state != QUIT) {
        // Handle user input
        handle_input(&chip8);

        // Emulate CHIP8 Instructions
        // -- TODO

        // Delay for 60hz (16.67ms)
        SDL_Delay(16);

        // Update window with changes
        update_screen(sdl);
    }

    // Final cleanup
    final_cleanup(sdl);

    exit(EXIT_SUCCESS);
}