#include "render/gpu_preference.hpp"
#include <SDL3/SDL.h>
#include <cstdio>

RON_EXPORT_GPU_PREFERENCE()

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    int version = SDL_GetVersion();
    int major = SDL_VERSIONNUM_MAJOR(version);
    int minor = SDL_VERSIONNUM_MINOR(version);
    int micro = SDL_VERSIONNUM_MICRO(version);

    const char* driver = SDL_GetCurrentVideoDriver();
    int display_count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&display_count);
    if (displays) {
        SDL_free(displays);
    }

    std::printf("SDL %d.%d.%d driver=%s displays=%d\n",
                major, minor, micro,
                driver ? driver : "null",
                display_count);

    SDL_Quit();

    if (!driver) {
        return 1;
    }
    return 0;
}
