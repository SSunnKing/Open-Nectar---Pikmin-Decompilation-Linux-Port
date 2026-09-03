#include "installer_ui.h"

#include <SDL.h>
#include <cstdio>
#include <string>

int main()
{
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    std::string error;
    pikmin::launcher::InstallerWindow window;
    if (!window.open(error)) {
        std::fprintf(stderr, "installer_ui_test: %s\n", error.c_str());
        return 1;
    }
    window.updateProgress(50, "dataDir/test/file.bin");
    std::puts("installer_ui_test: window and progress rendering passed");
    return 0;
}
