/**
 * @file pc_settings.cpp
 * @brief Settings menu for the Pikmin PC port (opened with F1).
 *
 * Fully PC-only. Renders through the game's GX/GL stack so it shares the same
 * visual language as the rest of the game, and persists a small config file so
 * video preferences survive restarts. Video changes are applied immediately but
 * guarded by an on-screen confirm/revert dialog that auto-reverts on timeout.
 *
 * To disable entirely: remove the PIKI_PC_SETTINGS_MENU compile definition and
 * the three hook sites in pc_window.cpp / vi_stubs.cpp, then delete this file.
 */

#include "settings/pc_settings.h"

#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdarg>
#include <string>
#include <fstream>
#include <algorithm>
#include <vector>

#include "pc_window.h"
#include "gl/pc_gfx.h"
#include "Graphics.h"
#include "Font.h"
#include "Colour.h"
#include "Matrix4f.h"
#include "Geometry.h"
#include "system.h"
#include "types.h"

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

namespace {

constexpr const char* kConfigFilename = "pikmin_settings.conf";

struct PcConfig {
    int windowWidth = 1280;
    int windowHeight = 720;
    int displayMode = PC_WINDOW_FULLSCREEN_WINDOWED; // 0 windowed, 1 fullscreen, 2 borderless
    double refreshRate = 0.0;                        // 0 = auto (detect)
    bool vsync = true;                               // presentation pacing on/off
    float renderScale = 2.0f / 3.0f;                 // internal 3D resolution multiplier
    int aspectRatioMode = 0;                         // 0=auto, 1=4:3, 2=16:10, 3=16:9, 4=21:9

    // 60 FPS experimental mode (0=disabled/30fps, 1=enabled/60fps)
    int fpsMode = 0;

    // Control mode (0=Classic, 1=Mouse Cursor)
    int controlMode = PC_CONTROL_CLASSIC;

    // Keyboard bindings (scancodes for each action)
    int keyboardBindings[PC_KEY_ACT_COUNT];

    // Mouse sensitivity (0.1 - 5.0, default 1.0)
    float mouseSensitivity = 1.0f;

    // Stick dead zone (0 - 127, default 8)
    int stickDeadZone = 8;

    // Stick inversion flags (bitmask: bit 0=horizontal, bit 1=vertical)
    // 0 = normal, 1 = inverted
    int stickInvert = 0;  // bit 0: X, bit 1: Y
    int cStickInvert = 0; // bit 0: X, bit 1: Y

    // Gamepad button bindings (SDL GameController button IDs)
    int gamepadBindings[PC_KEY_ACT_COUNT];

    // Mod: chain Pikmin actions (0=off/faithful, 1=on).
    // Off by default. The stock behaviour -- finish a job, walk back to the
    // squad -- is what the retail game does; this only changes it on request.
    int chainActions = 0;

    void applyDefaults() {
        windowWidth = 1280;
        windowHeight = 720;
        displayMode = PC_WINDOW_FULLSCREEN_WINDOWED;
        refreshRate = 0.0;
        vsync = true;
        renderScale = 2.0f / 3.0f;
        aspectRatioMode = 0;
        fpsMode = 0;
        controlMode = PC_CONTROL_CLASSIC;
        mouseSensitivity = 1.0f;
        stickDeadZone = 8;
        stickInvert = 0;
        cStickInvert = 0;
        chainActions = 0;
        for (int i = 0; i < PC_KEY_ACT_COUNT; i++) {
            keyboardBindings[i] = kDefaultKeyBindings[i];
            gamepadBindings[i] = -1; // -1 = not remapped (use default)
        }
    }
};

PcConfig sConfig;      // the confirmed, persisted settings
PcConfig sPending;     // settings staged while editing

// ---------------------------------------------------------------------------
// Menu state
// ---------------------------------------------------------------------------

enum Row {
    ROW_DISPLAY_MODE = 0,
    ROW_RESOLUTION,
    ROW_ASPECT_RATIO,
    ROW_RENDER_SCALE,
    ROW_REFRESH_RATE,
    ROW_VSYNC,
    ROW_FPS_MODE,
    ROW_CONTROLS,
    ROW_GAMEPAD,
    ROW_ADVANCED,
    ROW_MODS,
    ROW_RESET,
    ROW_SAVE,
    ROW_CLOSE,
    ROW_COUNT,
};

// La lista de resoluciones se construye en ejecucion a partir de lo que el
// monitor declara, en vez de la tabla fija que habia antes (un 4:3 y seis
// 16:9). Aquella dejaba sin ninguna entrada util a los paneles 16:10, 21:9 y a
// los portatiles con tamanos raros: la imagen no se deformaba —el recorte se
// centra con barras en calculate_output_area()— pero se desperdiciaba pantalla,
// y en pantalla completa exclusiva se pedia un modo de video que el monitor
// podia no admitir.
struct Resolution {
    int w;
    int h;
    bool isNative;  // coincide con el modo de escritorio
    bool isDerived; // fraccion de la nativa: vale como ventana, no como modo de video
};

std::vector<Resolution> sResolutions;
int sDesktopW = 0;
int sDesktopH = 0;
bool sHadConfigFile = false;

bool sMenuOpen = false;
int sSelection = ROW_DISPLAY_MODE;
static std::vector<Uint8> gPrevKeys; // previous-frame keyboard state snapshot

// Video confirm/revert dialog state.
bool sVideoConfirmActive = false;
// Reloj de pared, no fotogramas ni sondeos. Antes esto contaba llamadas a
// pc_settings_consume_game_input(), que se invoca desde pc_window_poll_events()
// -- y a esa la llaman DOS sitios por fotograma: el retrazo (vi_stubs) y cada
// lectura del mando (pad_stubs). El contador avanzaba al doble o mas, y los
// "8 segundos" se agotaban en tres o cuatro. Con SDL_GetTicks() el plazo es el
// mismo pase lo que pase con la tasa de refresco o el sondeo del mando.
Uint32 sVideoConfirmStartMs = 0;
constexpr Uint32 kVideoConfirmDurationMs = 8000;

// Lazy font state.
Font* sFont = nullptr;
bool sFontTried = false;

int sResolutionIdx = 0; // se resuelve al construir la lista (ver defaultResolutionIndex)

// Controls submenu state.
bool sInControlsSubmenu = false;
int sControlSelection = 0; // index into PC_KEY_ACT_COUNT
bool sWaitingForKey = false; // true while capturing a new key

// Gamepad controls submenu state.
bool sInGamepadSubmenu = false;
int sGamepadSelection = 0;
bool sWaitingForButton = false;

// Advanced settings submenu state.
bool sInAdvancedSubmenu = false;
int sAdvancedSelection = 0; // 0=sensitivity, 1=dead zone, 2=stick invert, 3=c-stick invert
constexpr int kAdvancedRowCount = 4;

// Mods submenu state. Everything here changes how the game *plays* rather than
// how it looks or reads input hardware, so it lives apart from the rest: a
// player who wants the original experience only has to leave this one page
// alone. 0=control scheme, 1=chain Pikmin actions.
bool sInModsSubmenu = false;
int sModsSelection = 0;
constexpr int kModsRowCount = 2;

// Submenu de resolucion. La lista sale del monitor, asi que puede traer veinte
// o cuarenta entradas segun el panel: recorrerlas de una en una con
// izquierda/derecha en la fila principal era inviable. `sResolutionChoices`
// guarda los indices de `sResolutions` validos para el modo de pantalla actual,
// resueltos al abrir, de modo que la lista solo enseña lo que de verdad se
// puede elegir.
bool sInResolutionSubmenu = false;
int sResolutionSubmenuSel = 0;
std::vector<int> sResolutionChoices;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Nombra la forma del panel para que la fila diga de un vistazo si una entrada
// encaja con el monitor. Es justo lo que faltaba cuando la lista era fija.
void aspectLabel(int w, int h, char* out, size_t n) {
    if (w <= 0 || h <= 0) { snprintf(out, n, "?"); return; }
    int a = w, b = h;
    while (b) { const int t = a % b; a = b; b = t; }
    const int rw = w / a, rh = h / a;
    // Nombres comerciales: la reduccion exacta de 16:10 es 8:5 y la de 21:9 es
    // 64:27 o 43:18 segun el panel, pero nadie los reconoce escritos asi.
    if (rw == 8 && rh == 5) { snprintf(out, n, "16:10"); return; }
    if ((rw == 64 && rh == 27) || (rw == 43 && rh == 18)) { snprintf(out, n, "21:9"); return; }
    if (rw == 32 && rh == 9) { snprintf(out, n, "32:9"); return; }
    if (rw <= 64 && rh <= 64) { snprintf(out, n, "%d:%d", rw, rh); return; }
    snprintf(out, n, "%.2f", float(w) / float(h));
}

void addResolution(int w, int h, bool derived) {
    if (w < 320 || h < 240) return;
    for (const Resolution& r : sResolutions) {
        if (r.w == w && r.h == h) return; // deduplicado por tamano: el refresco es otra fila
    }
    sResolutions.push_back({ w, h, w == sDesktopW && h == sDesktopH, derived });
}

void rebuildResolutionList() {
    sResolutions.clear();
    const int display = pc_window_get_display_index();

    SDL_DisplayMode desktop {};
    if (SDL_GetDesktopDisplayMode(display, &desktop) == 0) {
        sDesktopW = desktop.w;
        sDesktopH = desktop.h;
    }

    const int modeCount = SDL_GetNumDisplayModes(display);
    for (int i = 0; i < modeCount; i++) {
        SDL_DisplayMode dm {};
        if (SDL_GetDisplayMode(display, i, &dm) == 0) addResolution(dm.w, dm.h, false);
    }
    if (sDesktopW > 0) addResolution(sDesktopW, sDesktopH, false);

    // Una ventana no tiene por que coincidir con un modo de video, asi que se
    // ofrecen fracciones de la nativa: es la unica forma de tener una ventana
    // pequena con la forma del monitor.
    if (sDesktopW > 0) {
        const float fractions[] = { 0.75f, 2.0f / 3.0f, 0.5f };
        for (float f : fractions) {
            addResolution(int(sDesktopW * f) & ~1, int(sDesktopH * f) & ~1, true);
        }
    }

    if (sResolutions.empty()) addResolution(1280, 720, false); // ultimo recurso

    std::sort(sResolutions.begin(), sResolutions.end(),
              [](const Resolution& a, const Resolution& b) {
                  return a.w != b.w ? a.w > b.w : a.h > b.h;
              });
}

// Pantalla completa exclusiva cambia el modo de video de verdad, asi que solo
// admite modos que el monitor declara. En ventana no tiene sentido ofrecer
// tamanos mayores que el escritorio.
bool resolutionSelectable(const Resolution& r, int displayMode) {
    if (displayMode == PC_WINDOW_FULLSCREEN_EXCLUSIVE) return !r.isDerived;
    if (sDesktopW > 0 && (r.w > sDesktopW || r.h > sDesktopH)) return false;
    return true;
}

int resolutionIndexFor(int w, int h) {
    for (size_t i = 0; i < sResolutions.size(); i++) {
        if (sResolutions[i].w == w && sResolutions[i].h == h) return (int)i;
    }
    return -1;
}

void openResolutionSubmenu() {
    sResolutionChoices.clear();
    for (size_t i = 0; i < sResolutions.size(); i++) {
        if (resolutionSelectable(sResolutions[i], sPending.displayMode)) {
            sResolutionChoices.push_back((int)i);
        }
    }
    sResolutionSubmenuSel = 0;
    for (size_t k = 0; k < sResolutionChoices.size(); k++) {
        const Resolution& r = sResolutions[sResolutionChoices[k]];
        if (r.w == sPending.windowWidth && r.h == sPending.windowHeight) {
            sResolutionSubmenuSel = (int)k;
            break;
        }
    }
    sInResolutionSubmenu = true;
}

int defaultResolutionIndex() {
    for (size_t i = 0; i < sResolutions.size(); i++) {
        if (sResolutions[i].isNative) return (int)i;
    }
    return 0;
}

bool isVideoSettingChanged() {
    return sPending.windowWidth != sConfig.windowWidth ||
           sPending.windowHeight != sConfig.windowHeight ||
           sPending.displayMode != sConfig.displayMode ||
           sPending.vsync != sConfig.vsync ||
           sPending.renderScale != sConfig.renderScale ||
           sPending.aspectRatioMode != sConfig.aspectRatioMode ||
           (fabs(sPending.refreshRate - sConfig.refreshRate) > 0.5);
}

void applyVideo() {
    pc_window_set_display_mode(sPending.displayMode);
    pc_window_set_window_size(sPending.windowWidth, sPending.windowHeight);
    double rate = sPending.refreshRate;
    if (rate <= 0.0) rate = pc_window_get_refresh_rate(); // keep auto/detected
    pc_window_set_refresh_rate(rate);
    pc_window_set_vsync_enabled(sPending.vsync);
    pc_gfx_set_render_scale(sPending.renderScale);
    pc_gfx_set_aspect_ratio_mode(sPending.aspectRatioMode);
}

void applyControls(const PcConfig& config) {
    pc_window_set_control_mode(config.controlMode);
    pc_window_set_mouse_sensitivity(config.mouseSensitivity);
    pc_window_set_stick_dead_zone(config.stickDeadZone);
    pc_window_set_stick_invert(config.stickInvert);
    pc_window_set_cstick_invert(config.cStickInvert);
    for (int i = 0; i < PC_KEY_ACT_COUNT; i++) {
        pc_window_set_key_binding(i, static_cast<SDL_Scancode>(config.keyboardBindings[i]));
        pc_window_set_gamepad_binding(i, config.gamepadBindings[i]);
    }
}

void startVideoConfirm() {
    sVideoConfirmStartMs = SDL_GetTicks();
    sVideoConfirmActive = true;
}

void saveConfig(); // defined below

void confirmVideoSettings() {
    sConfig = sPending;
    applyVideo();
    applyControls(sConfig);
    saveConfig();
    sVideoConfirmActive = false;
}

// Tras restaurar valores hay que reapuntar el indice: si no, el siguiente
// izquierda/derecha saltaria desde la entrada que se acaba de rechazar.
void syncResolutionIndex() {
    const int idx = resolutionIndexFor(sPending.windowWidth, sPending.windowHeight);
    sResolutionIdx = idx >= 0 ? idx : defaultResolutionIndex();
}

void revertVideoSettings() {
    sPending = sConfig;
    applyVideo();
    syncResolutionIndex();
    sVideoConfirmActive = false;
}

void closeMenu() {
    // Revert any video settings that were not confirmed.
    if (sVideoConfirmActive) {
        revertVideoSettings();
    } else if (isVideoSettingChanged()) {
        sPending = sConfig;
        applyVideo();
        syncResolutionIndex();
    }
    // No dejar el menu memorizado dentro de la lista: al reabrir F1 se espera
    // la pagina principal.
    sInResolutionSubmenu = false;
    sMenuOpen = false;
    pc_window_set_settings_menu_open(false);
}

void resetToDefaults() {
    sConfig.applyDefaults();
    sPending = sConfig;
    applyVideo();
    applyControls(sConfig);
    saveConfig();
    sVideoConfirmActive = false;
}

void saveConfig() {
    std::string path = std::string(kConfigFilename);
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out) {
        printf("[PC Settings] Failed to write %s\n", path.c_str());
        return;
    }
    out << "# Open Nectar settings (F1 in-game to change)\n";
    out << "windowWidth = " << sConfig.windowWidth << "\n";
    out << "windowHeight = " << sConfig.windowHeight << "\n";
    out << "displayMode = " << sConfig.displayMode << "\n";
    out << "aspectRatioMode = " << sConfig.aspectRatioMode << "\n";
    out << "refreshRate = " << sConfig.refreshRate << "\n";
    out << "vsync = " << (sConfig.vsync ? 1 : 0) << "\n";
    out << "renderScale = " << sConfig.renderScale << "\n";
    out << "fpsMode = " << sConfig.fpsMode << "\n";
    out << "chainActions = " << sConfig.chainActions << "\n";
    out << "controlMode = " << sConfig.controlMode << "\n";
    out << "mouseSensitivity = " << sConfig.mouseSensitivity << "\n";
    out << "stickDeadZone = " << sConfig.stickDeadZone << "\n";
    out << "stickInvert = " << sConfig.stickInvert << "\n";
    out << "cStickInvert = " << sConfig.cStickInvert << "\n";
    // Keyboard bindings
    for (int i = 0; i < PC_KEY_ACT_COUNT; i++) {
        out << "key_" << i << " = " << sConfig.keyboardBindings[i] << "\n";
    }
    // Gamepad bindings
    for (int i = 0; i < PC_KEY_ACT_COUNT; i++) {
        out << "gp_" << i << " = " << sConfig.gamepadBindings[i] << "\n";
    }
    out.close();
    printf("[PC Settings] Saved %s\n", path.c_str());
}

void loadConfig() {
    sConfig.applyDefaults();
    std::string path = std::string(kConfigFilename);
    std::ifstream in(path, std::ios::in);
    if (!in) {
        printf("[PC Settings] No config file (%s); using defaults.\n", path.c_str());
        sHadConfigFile = false;
        return;
    }
    sHadConfigFile = true;
    std::string line;
    while (std::getline(in, line)) {
        size_t a = line.find_first_not_of(" \t");
        if (a == std::string::npos) continue;
        size_t b = line.find_last_not_of(" \t\r\n");
        if (b == std::string::npos) continue;
        line = line.substr(a, b - a + 1);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        size_t ka = key.find_first_not_of(" \t");
        size_t kb = key.find_last_not_of(" \t");
        key = key.substr(ka, kb - ka + 1);
        size_t va = val.find_first_not_of(" \t");
        size_t vb = val.find_last_not_of(" \t\r\n");
        val = (vb == std::string::npos) ? "" : val.substr(va, vb - va + 1);

        if (key == "windowWidth") sConfig.windowWidth = atoi(val.c_str());
        else if (key == "windowHeight") sConfig.windowHeight = atoi(val.c_str());
        else if (key == "displayMode") sConfig.displayMode = atoi(val.c_str());
        else if (key == "refreshRate") sConfig.refreshRate = atof(val.c_str());
        else if (key == "vsync") sConfig.vsync = atoi(val.c_str()) != 0;
        else if (key == "renderScale") {
            sConfig.renderScale = (float)atof(val.c_str());
            if (sConfig.renderScale < 0.25f || sConfig.renderScale > 4.0f) sConfig.renderScale = 2.0f / 3.0f;
        }
        else if (key == "controlMode") {
            sConfig.controlMode = atoi(val.c_str());
            if (sConfig.controlMode < PC_CONTROL_CLASSIC || sConfig.controlMode > PC_CONTROL_MOUSE_CURSOR) {
                sConfig.controlMode = PC_CONTROL_CLASSIC;
            }
        }
        else if (key == "mouseSensitivity") {
            sConfig.mouseSensitivity = (float)atof(val.c_str());
            if (sConfig.mouseSensitivity < 0.1f) sConfig.mouseSensitivity = 0.1f;
            if (sConfig.mouseSensitivity > 5.0f) sConfig.mouseSensitivity = 5.0f;
        }
        else if (key == "stickDeadZone") {
            sConfig.stickDeadZone = atoi(val.c_str());
            if (sConfig.stickDeadZone < 0) sConfig.stickDeadZone = 0;
            if (sConfig.stickDeadZone > 127) sConfig.stickDeadZone = 127;
        }
        else if (key == "aspectRatioMode") {
            sConfig.aspectRatioMode = atoi(val.c_str());
            if (sConfig.aspectRatioMode < 0) sConfig.aspectRatioMode = 0;
            if (sConfig.aspectRatioMode > 4) sConfig.aspectRatioMode = 4;
        }
        else if (key == "fpsMode") {
            sConfig.fpsMode = atoi(val.c_str());
            if (sConfig.fpsMode < 0) sConfig.fpsMode = 0;
            if (sConfig.fpsMode > 1) sConfig.fpsMode = 1;
        }
        else if (key == "chainActions") {
            sConfig.chainActions = atoi(val.c_str()) ? 1 : 0;
        }
        else if (key == "stickInvert") sConfig.stickInvert = atoi(val.c_str()) & 3;
        else if (key == "cStickInvert") sConfig.cStickInvert = atoi(val.c_str()) & 3;
        else if (key.rfind("key_", 0) == 0) {
            int idx = atoi(key.substr(4).c_str());
            if (idx >= 0 && idx < PC_KEY_ACT_COUNT) {
                const int scancode = atoi(val.c_str());
                if (scancode >= 0 && scancode < SDL_NUM_SCANCODES) {
                    sConfig.keyboardBindings[idx] = scancode;
                }
            }
        }
        else if (key.rfind("gp_", 0) == 0) {
            int idx = atoi(key.substr(3).c_str());
            if (idx >= 0 && idx < PC_KEY_ACT_COUNT) {
                const int button = atoi(val.c_str());
                if (button >= -1 && button < SDL_CONTROLLER_BUTTON_MAX) {
                    sConfig.gamepadBindings[idx] = button;
                }
            }
        }
    }
    in.close();
    if (sConfig.windowWidth <= 0) sConfig.windowWidth = 1280;
    if (sConfig.windowHeight <= 0) sConfig.windowHeight = 720;
    printf("[PC Settings] Loaded %s: %dx%d mode=%d vsync=%d refresh=%.0f\n",
           path.c_str(), sConfig.windowWidth, sConfig.windowHeight,
           sConfig.displayMode, sConfig.vsync ? 1 : 0, sConfig.refreshRate);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool keyWentDown(SDL_Scancode sc) {
    int numKeys = 0;
    const Uint8* state = SDL_GetKeyboardState(&numKeys);
    bool now = (int)sc < numKeys && state[sc] != 0;
    bool prev = (int)sc < (int)gPrevKeys.size() && gPrevKeys[sc] != 0;
    return now && !prev;
}

void latchKeys() {
    int numKeys = 0;
    const Uint8* state = SDL_GetKeyboardState(&numKeys);
    if (numKeys < 0) numKeys = 0;
    gPrevKeys.assign(state, state + numKeys);
}

void pollMenuInput() {
    // F1 toggles the menu.
    if (keyWentDown(SDL_SCANCODE_F1)) {
        if (sMenuOpen) {
            closeMenu();
        } else {
            sPending = sConfig;
            sPending.controlMode = pc_window_get_control_mode();
            sMenuOpen = true;
            pc_window_set_settings_menu_open(true);
            sSelection = ROW_DISPLAY_MODE;
            sVideoConfirmActive = false;
            rebuildResolutionList();
            const int idx = resolutionIndexFor(pc_window_get_width(), pc_window_get_height());
            sResolutionIdx = idx >= 0 ? idx : defaultResolutionIndex();
        }
        return;
    }

    if (!sMenuOpen) return;

    // Poll gamepad state for menu navigation.
    SDL_GameController* ctl = pc_window_get_controller();

    // Modal video-confirm dialog.
    if (sVideoConfirmActive) {
        // Auto-revert on timeout. La resta sin signo se comporta bien cuando
        // SDL_GetTicks() da la vuelta.
        const Uint32 elapsed = SDL_GetTicks() - sVideoConfirmStartMs;
        if (elapsed >= kVideoConfirmDurationMs) {
            revertVideoSettings();
            return;
        }
        if (keyWentDown(SDL_SCANCODE_RETURN) || keyWentDown(SDL_SCANCODE_SPACE) ||
            keyWentDown(SDL_SCANCODE_J) ||
            (ctl && SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_A))) {
            confirmVideoSettings();
        } else if (keyWentDown(SDL_SCANCODE_ESCAPE) || keyWentDown(SDL_SCANCODE_K) ||
                   keyWentDown(SDL_SCANCODE_B) ||
                   (ctl && SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_B))) {
            revertVideoSettings();
        }
        return;
    }

    // Controls submenu (key capture mode).
    if (sInControlsSubmenu) {
        if (sWaitingForKey) {
            // Wait for any key press.
            const Uint8* state = SDL_GetKeyboardState(NULL);
            for (int sc = 0; sc < SDL_NUM_SCANCODES; sc++) {
                if (state[sc]) {
                    // Ignore modifier keys alone.
                    if (sc != SDL_SCANCODE_LCTRL && sc != SDL_SCANCODE_RCTRL &&
                        sc != SDL_SCANCODE_LSHIFT && sc != SDL_SCANCODE_RSHIFT &&
                        sc != SDL_SCANCODE_LALT && sc != SDL_SCANCODE_RALT &&
                        sc != SDL_SCANCODE_LGUI && sc != SDL_SCANCODE_RGUI) {
                        sPending.keyboardBindings[sControlSelection] = sc;
                        sWaitingForKey = false;
                        break;
                    }
                }
            }
            // ESC cancels capture.
            if (keyWentDown(SDL_SCANCODE_ESCAPE) || (ctl && SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_B))) {
                sWaitingForKey = false;
            }
            return;
        }

        // Navigation in controls list.
        bool up = keyWentDown(SDL_SCANCODE_UP) || keyWentDown(SDL_SCANCODE_W);
        bool down = keyWentDown(SDL_SCANCODE_DOWN) || keyWentDown(SDL_SCANCODE_S);
        bool left = keyWentDown(SDL_SCANCODE_LEFT) || keyWentDown(SDL_SCANCODE_A);
        bool right = keyWentDown(SDL_SCANCODE_RIGHT) || keyWentDown(SDL_SCANCODE_D);
        bool ok = keyWentDown(SDL_SCANCODE_RETURN) || keyWentDown(SDL_SCANCODE_SPACE);

        if (ctl) {
            if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_DPAD_UP) ||
                (SDL_GameControllerGetAxis(ctl, SDL_CONTROLLER_AXIS_LEFTY) < -8000))
                up = true;
            if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_DPAD_DOWN) ||
                (SDL_GameControllerGetAxis(ctl, SDL_CONTROLLER_AXIS_LEFTY) > 8000))
                down = true;
            if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_DPAD_LEFT) ||
                (SDL_GameControllerGetAxis(ctl, SDL_CONTROLLER_AXIS_LEFTX) < -8000))
                left = true;
            if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ||
                (SDL_GameControllerGetAxis(ctl, SDL_CONTROLLER_AXIS_LEFTX) > 8000))
                right = true;
            if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_A))
                ok = true;
        }

        if (up) {
            sControlSelection = (sControlSelection + PC_KEY_ACT_COUNT - 1) % PC_KEY_ACT_COUNT;
            return;
        }
        if (down) {
            sControlSelection = (sControlSelection + 1) % PC_KEY_ACT_COUNT;
            return;
        }
        if (ok) {
            sWaitingForKey = true;
            return;
        }
        if (left || right) {
            // Reset to default on left/right.
            sPending.keyboardBindings[sControlSelection] = kDefaultKeyBindings[sControlSelection];
            return;
        }
        // B / ESC exits submenu.
        if (keyWentDown(SDL_SCANCODE_ESCAPE) || keyWentDown(SDL_SCANCODE_K) ||
            keyWentDown(SDL_SCANCODE_B) || (ctl && SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_B))) {
            sInControlsSubmenu = false;
            sWaitingForKey = false;
        }
        return;
    }

    // Gamepad submenu (button capture mode).
    if (sInGamepadSubmenu) {
        if (sWaitingForButton) {
            // Wait for any gamepad button press.
            if (ctl) {
                for (int btn = 0; btn < SDL_CONTROLLER_BUTTON_MAX; btn++) {
                    if (SDL_GameControllerGetButton(ctl, (SDL_GameControllerButton)btn)) {
                        sPending.gamepadBindings[sGamepadSelection] = btn;
                        sWaitingForButton = false;
                        break;
                    }
                }
            }
            // ESC cancels capture.
            if (keyWentDown(SDL_SCANCODE_ESCAPE) || (ctl && SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_B))) {
                sWaitingForButton = false;
            }
            return;
        }

        // Navigation in gamepad list.
        bool up = keyWentDown(SDL_SCANCODE_UP) || keyWentDown(SDL_SCANCODE_W);
        bool down = keyWentDown(SDL_SCANCODE_DOWN) || keyWentDown(SDL_SCANCODE_S);
        bool left = keyWentDown(SDL_SCANCODE_LEFT) || keyWentDown(SDL_SCANCODE_A);
        bool right = keyWentDown(SDL_SCANCODE_RIGHT) || keyWentDown(SDL_SCANCODE_D);
        bool ok = keyWentDown(SDL_SCANCODE_RETURN) || keyWentDown(SDL_SCANCODE_SPACE);

        if (ctl) {
            if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_DPAD_UP) ||
                (SDL_GameControllerGetAxis(ctl, SDL_CONTROLLER_AXIS_LEFTY) < -8000))
                up = true;
            if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_DPAD_DOWN) ||
                (SDL_GameControllerGetAxis(ctl, SDL_CONTROLLER_AXIS_LEFTY) > 8000))
                down = true;
            if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_DPAD_LEFT) ||
                (SDL_GameControllerGetAxis(ctl, SDL_CONTROLLER_AXIS_LEFTX) < -8000))
                left = true;
            if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ||
                (SDL_GameControllerGetAxis(ctl, SDL_CONTROLLER_AXIS_LEFTX) > 8000))
                right = true;
            if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_A))
                ok = true;
        }

        if (up) {
            sGamepadSelection = (sGamepadSelection + PC_KEY_ACT_COUNT - 1) % PC_KEY_ACT_COUNT;
            return;
        }
        if (down) {
            sGamepadSelection = (sGamepadSelection + 1) % PC_KEY_ACT_COUNT;
            return;
        }
        if (ok) {
            sWaitingForButton = true;
            return;
        }
        if (left || right) {
            sPending.gamepadBindings[sGamepadSelection] = -1;
            return;
        }
        if (keyWentDown(SDL_SCANCODE_ESCAPE) || keyWentDown(SDL_SCANCODE_K) ||
            keyWentDown(SDL_SCANCODE_B) || (ctl && SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_B))) {
            sInGamepadSubmenu = false;
            sWaitingForButton = false;
        }
        return;
    }

    // Advanced settings submenu.
    if (sInAdvancedSubmenu) {
        bool up = keyWentDown(SDL_SCANCODE_UP) || keyWentDown(SDL_SCANCODE_W);
        bool down = keyWentDown(SDL_SCANCODE_DOWN) || keyWentDown(SDL_SCANCODE_S);
        bool left = keyWentDown(SDL_SCANCODE_LEFT) || keyWentDown(SDL_SCANCODE_A);
        bool right = keyWentDown(SDL_SCANCODE_RIGHT) || keyWentDown(SDL_SCANCODE_D);
        bool ok = keyWentDown(SDL_SCANCODE_RETURN) || keyWentDown(SDL_SCANCODE_SPACE);
        bool cancel = keyWentDown(SDL_SCANCODE_ESCAPE) || keyWentDown(SDL_SCANCODE_K) ||
                      keyWentDown(SDL_SCANCODE_B);

        if (ctl) {
            if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_DPAD_UP) ||
                (SDL_GameControllerGetAxis(ctl, SDL_CONTROLLER_AXIS_LEFTY) < -8000))
                up = true;
            if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_DPAD_DOWN) ||
                (SDL_GameControllerGetAxis(ctl, SDL_CONTROLLER_AXIS_LEFTY) > 8000))
                down = true;
            if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_DPAD_LEFT) ||
                (SDL_GameControllerGetAxis(ctl, SDL_CONTROLLER_AXIS_LEFTX) < -8000))
                left = true;
            if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ||
                (SDL_GameControllerGetAxis(ctl, SDL_CONTROLLER_AXIS_LEFTX) > 8000))
                right = true;
            if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_A))
                ok = true;
            if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_B))
                cancel = true;
        }

        if (up) {
            sAdvancedSelection = (sAdvancedSelection + kAdvancedRowCount - 1) % kAdvancedRowCount;
            return;
        }
        if (down) {
            sAdvancedSelection = (sAdvancedSelection + 1) % kAdvancedRowCount;
            return;
        }
        if (cancel) {
            sInAdvancedSubmenu = false;
            return;
        }

        // Sensitivity (0.1 - 5.0, step 0.1)
        if (sAdvancedSelection == 0) {
            float step = 0.1f;
            if (left) sPending.mouseSensitivity = fmaxf(0.1f, sPending.mouseSensitivity - step);
            if (right) sPending.mouseSensitivity = fminf(5.0f, sPending.mouseSensitivity + step);
        }
        // Stick dead zone (0 - 127, step 4)
        else if (sAdvancedSelection == 1) {
            int step = 4;
            if (left) sPending.stickDeadZone = std::max(0, sPending.stickDeadZone - step);
            if (right) sPending.stickDeadZone = std::min(127, sPending.stickDeadZone + step);
        }
        // Stick invert (bitmask)
        else if (sAdvancedSelection == 2) {
            if (left || right) {
                sPending.stickInvert ^= 3; // toggle X and Y bits
            }
        }
        // C-stick invert (bitmask)
        else if (sAdvancedSelection == 3) {
            if (left || right) {
                sPending.cStickInvert ^= 3; // toggle X and Y bits
            }
        }
        return;
    }

    // Resolution submenu.
    if (sInResolutionSubmenu) {
        bool up = keyWentDown(SDL_SCANCODE_UP) || keyWentDown(SDL_SCANCODE_W);
        bool down = keyWentDown(SDL_SCANCODE_DOWN) || keyWentDown(SDL_SCANCODE_S);
        bool ok = keyWentDown(SDL_SCANCODE_RETURN) || keyWentDown(SDL_SCANCODE_SPACE);
        bool cancel = keyWentDown(SDL_SCANCODE_ESCAPE) || keyWentDown(SDL_SCANCODE_K) ||
                      keyWentDown(SDL_SCANCODE_B);

        if (ctl) {
            if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_DPAD_UP) ||
                (SDL_GameControllerGetAxis(ctl, SDL_CONTROLLER_AXIS_LEFTY) < -8000))
                up = true;
            if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_DPAD_DOWN) ||
                (SDL_GameControllerGetAxis(ctl, SDL_CONTROLLER_AXIS_LEFTY) > 8000))
                down = true;
            if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_A))
                ok = true;
            if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_B))
                cancel = true;
        }

        const int choiceCount = (int)sResolutionChoices.size();
        if (cancel || choiceCount == 0) {
            sInResolutionSubmenu = false;
            return;
        }
        if (up) {
            sResolutionSubmenuSel = (sResolutionSubmenuSel + choiceCount - 1) % choiceCount;
            return;
        }
        if (down) {
            sResolutionSubmenuSel = (sResolutionSubmenuSel + 1) % choiceCount;
            return;
        }
        if (ok) {
            const int idx = sResolutionChoices[sResolutionSubmenuSel];
            sResolutionIdx = idx;
            sPending.windowWidth = sResolutions[idx].w;
            sPending.windowHeight = sResolutions[idx].h;
            // Cerrar antes de aplicar: el dialogo de confirmacion se dibuja
            // sobre el menu principal y tiene prioridad sobre los submenus.
            sInResolutionSubmenu = false;
            applyVideo();
            startVideoConfirm();
        }
        return;
    }

    // Mods submenu.
    if (sInModsSubmenu) {
        bool up = keyWentDown(SDL_SCANCODE_UP) || keyWentDown(SDL_SCANCODE_W);
        bool down = keyWentDown(SDL_SCANCODE_DOWN) || keyWentDown(SDL_SCANCODE_S);
        bool left = keyWentDown(SDL_SCANCODE_LEFT) || keyWentDown(SDL_SCANCODE_A);
        bool right = keyWentDown(SDL_SCANCODE_RIGHT) || keyWentDown(SDL_SCANCODE_D);
        bool cancel = keyWentDown(SDL_SCANCODE_ESCAPE) || keyWentDown(SDL_SCANCODE_K) ||
                      keyWentDown(SDL_SCANCODE_B);

        if (ctl) {
            if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_DPAD_UP) ||
                (SDL_GameControllerGetAxis(ctl, SDL_CONTROLLER_AXIS_LEFTY) < -8000))
                up = true;
            if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_DPAD_DOWN) ||
                (SDL_GameControllerGetAxis(ctl, SDL_CONTROLLER_AXIS_LEFTY) > 8000))
                down = true;
            if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_DPAD_LEFT) ||
                (SDL_GameControllerGetAxis(ctl, SDL_CONTROLLER_AXIS_LEFTX) < -8000))
                left = true;
            if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ||
                (SDL_GameControllerGetAxis(ctl, SDL_CONTROLLER_AXIS_LEFTX) > 8000))
                right = true;
            if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_B))
                cancel = true;
        }

        if (up) {
            sModsSelection = (sModsSelection + kModsRowCount - 1) % kModsRowCount;
            return;
        }
        if (down) {
            sModsSelection = (sModsSelection + 1) % kModsRowCount;
            return;
        }
        if (cancel) {
            sInModsSubmenu = false;
            return;
        }

        // Control scheme: Classic (GameCube) or Mouse Cursor.
        if (sModsSelection == 0) {
            if (left) sPending.controlMode = (sPending.controlMode - 1 + 2) % 2;
            else if (right) sPending.controlMode = (sPending.controlMode + 1) % 2;
        }
        // Chain Pikmin actions.
        else if (sModsSelection == 1) {
            if (left || right) sPending.chainActions = sPending.chainActions ? 0 : 1;
        }
        return;
    }

    // Main menu navigation (existing logic below).
    bool up = keyWentDown(SDL_SCANCODE_UP) || keyWentDown(SDL_SCANCODE_W);
    bool down = keyWentDown(SDL_SCANCODE_DOWN) || keyWentDown(SDL_SCANCODE_S);
    bool left = keyWentDown(SDL_SCANCODE_LEFT) || keyWentDown(SDL_SCANCODE_A);
    bool right = keyWentDown(SDL_SCANCODE_RIGHT) || keyWentDown(SDL_SCANCODE_D);
    bool ok = keyWentDown(SDL_SCANCODE_RETURN) || keyWentDown(SDL_SCANCODE_SPACE);
    bool cancel = keyWentDown(SDL_SCANCODE_ESCAPE) || keyWentDown(SDL_SCANCODE_K) ||
                  keyWentDown(SDL_SCANCODE_B);

    if (ctl) {
        if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_DPAD_UP) ||
            (SDL_GameControllerGetAxis(ctl, SDL_CONTROLLER_AXIS_LEFTY) < -8000))
            up = true;
        if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_DPAD_DOWN) ||
            (SDL_GameControllerGetAxis(ctl, SDL_CONTROLLER_AXIS_LEFTY) > 8000))
            down = true;
        if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_DPAD_LEFT) ||
            (SDL_GameControllerGetAxis(ctl, SDL_CONTROLLER_AXIS_LEFTX) < -8000))
            left = true;
        if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ||
            (SDL_GameControllerGetAxis(ctl, SDL_CONTROLLER_AXIS_LEFTX) > 8000))
            right = true;
        if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_A))
            ok = true;
        if (SDL_GameControllerGetButton(ctl, SDL_CONTROLLER_BUTTON_B))
            cancel = true;
    }

    if (up) {
        sSelection = (sSelection + ROW_COUNT - 1) % ROW_COUNT;
        return;
    }
    if (down) {
        sSelection = (sSelection + 1) % ROW_COUNT;
        return;
    }

    // Left/Right/OK/Cancel handling via switch.
    auto cycleResolution = [](int dir) {
        // Borderless siempre usa el escritorio, asi que la fila no se toca.
        if (sPending.displayMode == PC_WINDOW_FULLSCREEN_BORDERLESS) return;
        const int n = (int)sResolutions.size();
        if (n == 0) return;
        for (int step = 1; step <= n; step++) {
            const int idx = ((sResolutionIdx + dir * step) % n + n) % n;
            if (!resolutionSelectable(sResolutions[idx], sPending.displayMode)) continue;
            sResolutionIdx = idx;
            sPending.windowWidth = sResolutions[idx].w;
            sPending.windowHeight = sResolutions[idx].h;
            return;
        }
    };

    switch (sSelection) {
    case ROW_DISPLAY_MODE:
        if (left) sPending.displayMode = (sPending.displayMode + 3 - 1) % 3;
        else if (right) sPending.displayMode = (sPending.displayMode + 1) % 3;
        if (left || right) { applyVideo(); startVideoConfirm(); }
        break;
    case ROW_RESOLUTION:
        // Enter abre la lista completa; izquierda/derecha sigue sirviendo para
        // un salto rapido a la entrada contigua.
        if (ok && sPending.displayMode != PC_WINDOW_FULLSCREEN_BORDERLESS) {
            openResolutionSubmenu();
            break;
        }
        if (left) cycleResolution(-1);
        else if (right) cycleResolution(1);
        if (left || right) { applyVideo(); startVideoConfirm(); }
        break;
    case ROW_ASPECT_RATIO:
        if (left) sPending.aspectRatioMode = (sPending.aspectRatioMode + 5 - 1) % 5;
        else if (right) sPending.aspectRatioMode = (sPending.aspectRatioMode + 1) % 5;
        if (left || right) {
            pc_gfx_set_aspect_ratio_mode(sPending.aspectRatioMode);
            applyVideo();
            startVideoConfirm();
        }
        break;
    case ROW_RENDER_SCALE: {
        static const float kScales[] = { 2.0f / 3.0f, 0.5f, 1.0f, 1.5f, 2.0f };
        constexpr int kScaleCount = 5;
        int idx = 0;
        float cur = sPending.renderScale;
        for (int i = 0; i < kScaleCount; i++) {
            if (fabsf(kScales[i] - cur) < 0.01f) { idx = i; break; }
        }
        if (left) idx = (idx + kScaleCount - 1) % kScaleCount;
        else if (right) idx = (idx + 1) % kScaleCount;
        sPending.renderScale = kScales[idx];
        if (left || right) { applyVideo(); startVideoConfirm(); }
        break;
    }
    case ROW_REFRESH_RATE: {
        static const double kRates[] = { 0.0, 60.0, 120.0, 144.0, 165.0, 240.0 };
        constexpr int kRateCount = 6;
        double cur = sPending.refreshRate;
        int idx = 0;
        for (int i = 0; i < kRateCount; i++) {
            if (fabs(kRates[i] - cur) < 0.5) { idx = i; break; }
        }
        if (left) idx = (idx + kRateCount - 1) % kRateCount;
        else if (right) idx = (idx + 1) % kRateCount;
        sPending.refreshRate = kRates[idx];
        if (left || right) { applyVideo(); startVideoConfirm(); }
        break;
    }
    case ROW_VSYNC:
        if (left || right || ok) {
            sPending.vsync = !sPending.vsync;
            applyVideo();
            startVideoConfirm();
        }
        break;
    case ROW_FPS_MODE:
        if (left) {
            sPending.fpsMode = (sPending.fpsMode - 1 + 2) % 2;
        } else if (right) {
            sPending.fpsMode = (sPending.fpsMode + 1) % 2;
        }
        break;
    case ROW_CONTROLS:
        if (ok) {
            sInControlsSubmenu = true;
            sControlSelection = 0;
            sWaitingForKey = false;
        }
        break;
    case ROW_GAMEPAD:
        if (ok) {
            sInGamepadSubmenu = true;
            sGamepadSelection = 0;
            sWaitingForButton = false;
        }
        break;
    case ROW_ADVANCED:
        if (ok) {
            sInAdvancedSubmenu = true;
            sAdvancedSelection = 0;
        }
        break;
    case ROW_MODS:
        if (ok) {
            sInModsSubmenu = true;
            sModsSelection = 0;
        }
        break;
    case ROW_RESET:
        if (ok) resetToDefaults();
        break;
    case ROW_SAVE:
        if (ok) {
            // Save pending changes (confirming video if applicable).
            if (sVideoConfirmActive) {
                confirmVideoSettings();
            } else {
                sConfig = sPending;
                applyVideo();
                applyControls(sConfig);
                saveConfig();
            }
        }
        break;
    case ROW_CLOSE:
        if (ok) closeMenu();
        break;
    default:
        break;
    }

    // Esc / B closes the menu (reverting unconfirmed changes).
    if (keyWentDown(SDL_SCANCODE_ESCAPE) || keyWentDown(SDL_SCANCODE_K) ||
        keyWentDown(SDL_SCANCODE_B)) {
        closeMenu();
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

int gDrawCursorX = 0;
int gDrawCursorY = 0;

void ensureFont() {
    if (sFontTried) return;
    sFontTried = true;
    if (gsys) {
        sFont = new Font;
        Texture* tex = gsys->loadTexture("consFont.bti", true);
        if (tex) {
            sFont->setTexture(tex, 16, 8);
        } else {
            delete sFont;
            sFont = nullptr;
        }
    }
}

void drawText(const char* fmt, ...) {
    char buf[512];
    va_list vl;
    va_start(vl, fmt);
    vsnprintf(buf, sizeof(buf), fmt, vl);
    va_end(vl);
    static_cast<DGXGraphics*>(gsys->mDGXGfx)->texturePrintf(sFont, gDrawCursorX, gDrawCursorY, buf);
}

Colour lerpColour(Colour a, Colour b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return Colour(int(a.r + (b.r - a.r) * t), int(a.g + (b.g - a.g) * t),
                 int(a.b + (b.b - a.b) * t), int(a.a + (b.a - a.a) * t));
}

// Filled rounded rectangle (all 4 corners radius r) built from 2px horizontal
// strips. Each strip's colour is lerped top->bottom to fake a vertical gradient.
void fillRoundRectGrad(DGXGraphics* gfx, int x, int y, int w, int h, int r,
                       Colour top, Colour bottom) {
    if (w <= 0 || h <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    const int step = 2;
    for (int yy = y; yy < y + h; yy += step) {
        int bandH = step;
        if (yy + bandH > y + h) bandH = y + h - yy;
        int t = yy - y; // 0..h
        // horizontal inset from the rounded corners
        int inset = 0;
        int lo = (t < r) ? t : (t > h - r ? h - t : -1);
        if (lo >= 0) {
            int dy = r - lo;                    // vertical distance to corner-circle center line
            int half = (int)floorf(sqrtf((float)(r * r - dy * dy)));
            inset = r - half;
        }
        Colour c = lerpColour(top, bottom, (float)(yy - y) / (float)h);
        gfx->setColour(c, true);
        gfx->setAuxColour(c);
        gfx->fillRectangle(RectArea(x + inset, yy, x + w - inset, yy + bandH));
    }
}

// Text with a heavy dark outline (drawn offset in shadow colour, then main).
void drawTextOutline(int x, int y, const char* fmt, Colour main, Colour shadow, ...) {
    char buf[512];
    va_list vl;
    va_start(vl, shadow);
    vsnprintf(buf, sizeof(buf), fmt, vl);
    va_end(vl);

    DGXGraphics* gfx = static_cast<DGXGraphics*>(gsys->mDGXGfx);
    gfx->setColour(shadow, true);
    gfx->setAuxColour(shadow);
    for (int ox = -2; ox <= 2; ox++) {
        for (int oy = -1; oy <= 1; oy++) {
            gDrawCursorX = x + ox; gDrawCursorY = y + oy;
            drawText("%s", buf);
        }
    }
    gfx->setColour(main, true);
    gfx->setAuxColour(main);
    gDrawCursorX = x; gDrawCursorY = y;
    drawText("%s", buf);
}

void drawPikminPanel(DGXGraphics* gfx, int x, int y, int w, int h, int radius) {
    // Soft offset shadow, then the broad silver/black bezel used throughout
    // Pikmin's menus. Layering rounded fills keeps this independent of assets.
    fillRoundRectGrad(gfx, x + 7, y + 9, w, h, radius,
                      Colour(0, 0, 0, 150), Colour(0, 0, 0, 220));
    fillRoundRectGrad(gfx, x, y, w, h, radius,
                      Colour(225, 232, 242, 245), Colour(54, 58, 66, 255));
    fillRoundRectGrad(gfx, x + 3, y + 4, w - 6, h - 8, radius - 3,
                      Colour(32, 34, 40, 255), Colour(3, 4, 7, 255));
    fillRoundRectGrad(gfx, x + 9, y + 10, w - 18, h - 20, radius - 8,
                      Colour(41, 49, 83, 248), Colour(12, 17, 35, 252));

    // Reflected strip along the upper inner edge.
    fillRoundRectGrad(gfx, x + 18, y + 12, w - 36, 16, 9,
                      Colour(255, 255, 255, 76), Colour(128, 151, 196, 4));
}

void drawPikminHeader(DGXGraphics* gfx, int panelX, int panelY, int panelW,
                      const char* title) {
    int titleW = sFont->stringWidth(title);
    int w = titleW + 92;
    if (w < 250) w = 250;
    if (w > panelW - 70) w = panelW - 70;
    int x = panelX + (panelW - w) / 2;
    int y = panelY - 18;

    fillRoundRectGrad(gfx, x + 5, y + 7, w, 52, 18,
                      Colour(0, 0, 0, 130), Colour(0, 0, 0, 210));
    fillRoundRectGrad(gfx, x, y, w, 52, 18,
                      Colour(224, 230, 238, 220), Colour(66, 70, 78, 245));
    fillRoundRectGrad(gfx, x + 3, y + 4, w - 6, 44, 15,
                      Colour(20, 22, 27, 248), Colour(2, 3, 5, 252));
    fillRoundRectGrad(gfx, x + 10, y + 8, w - 20, 16, 10,
                      Colour(255, 255, 255, 92), Colour(255, 255, 255, 2));

    drawTextOutline(panelX + panelW / 2 - titleW / 2, y + 20, "%s",
                    Colour(218, 255, 255, 255), Colour(0, 8, 12, 255), title);
}

void drawSubmenuSurface(DGXGraphics* gfx, int x, int y, int w, int h,
                        const char* title, const char* helpTop,
                        const char* helpBottom) {
    // Opaque surface: the parent settings must not remain legible through a
    // child page. The old translucent rectangle caused both lists to overlap.
    fillRoundRectGrad(gfx, x, y, w, h, 14,
                      Colour(5, 7, 12, 255), Colour(0, 1, 4, 255));
    fillRoundRectGrad(gfx, x + 4, y + 4, w - 8, 30, 11,
                      Colour(74, 84, 112, 255), Colour(18, 23, 40, 255));
    int titleX = x + w / 2 - sFont->stringWidth(title) / 2;
    drawTextOutline(titleX, y + 12, "%s", Colour(255, 207, 75, 255),
                    Colour(49, 20, 0, 255), title);

    int helpY = y + h - 39;
    drawTextOutline(x + w / 2 - sFont->stringWidth(helpTop) / 2, helpY,
                    "%s", Colour(205, 239, 250, 255), Colour(0, 8, 13, 255), helpTop);
    drawTextOutline(x + w / 2 - sFont->stringWidth(helpBottom) / 2, helpY + 16,
                    "%s", Colour(205, 239, 250, 255), Colour(0, 8, 13, 255), helpBottom);
}

void drawSubmenuRow(DGXGraphics* gfx, int x, int y, int w,
                    const char* label, const char* value, bool selected) {
    if (selected) {
        fillRoundRectGrad(gfx, x, y - 3, w, 22, 8,
                          Colour(58, 51, 31, 235), Colour(7, 7, 8, 245));
        drawTextOutline(x + 10, y, ">", Colour(255, 229, 120, 255),
                        Colour(48, 18, 0, 255));
    }
    Colour main = selected ? Colour(255, 190, 28, 255) : Colour(185, 237, 255, 255);
    Colour shadow = selected ? Colour(62, 25, 0, 255) : Colour(0, 9, 15, 255);
    const int split = x + w / 2;
    drawTextOutline(split - 14 - sFont->stringWidth(label), y, "%s",
                    main, shadow, label);
    drawTextOutline(split + 14, y, "%s", main, shadow, value);
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void pc_settings_init(void) {
    loadConfig();
    sPending = sConfig;
    rebuildResolutionList();
    // Sin fichero previo, arrancar a la resolucion del monitor en vez de a un
    // 1280x720 fijo que en un panel 16:10 o 21:9 deja barras desde el principio.
    int idx = sHadConfigFile ? resolutionIndexFor(sPending.windowWidth, sPending.windowHeight) : -1;
    if (idx < 0) {
        idx = defaultResolutionIndex();
        if (!sResolutions.empty()) {
            sPending.windowWidth = sResolutions[idx].w;
            sPending.windowHeight = sResolutions[idx].h;
            sConfig.windowWidth = sPending.windowWidth;
            sConfig.windowHeight = sPending.windowHeight;
        }
    }
    sResolutionIdx = idx;
    // Apply persisted display settings at startup.
    pc_window_set_vsync_enabled(sPending.vsync);
    pc_window_set_display_mode(sPending.displayMode);
    pc_window_set_window_size(sPending.windowWidth, sPending.windowHeight);
    if (sPending.refreshRate > 0.0) pc_window_set_refresh_rate(sPending.refreshRate);
    pc_gfx_set_render_scale(sPending.renderScale);
    pc_gfx_set_aspect_ratio_mode(sPending.aspectRatioMode);
    applyControls(sConfig);
    printf("[PC Settings] Init complete.\n");
}

bool pc_settings_consume_game_input(void) {
    pollMenuInput();   // edge-detect using the previous frame's snapshot
    latchKeys();       // snapshot AFTER polling so next frame sees this one
    return sMenuOpen;
}

void pc_settings_apply_video(void) {
    if (!sVideoConfirmActive && isVideoSettingChanged()) {
        applyVideo();
    }
}

bool pc_settings_has_pending_video(void) {
    return sVideoConfirmActive;
}

void pc_settings_draw(void) {
    if (!sMenuOpen) return;
    if (!gsys || !gsys->mDGXGfx) return;
    DGXGraphics* gfx = static_cast<DGXGraphics*>(gsys->mDGXGfx);
    ensureFont();
    if (!sFont) return;

    const int screenW = gfx->mScreenWidth;
    const int screenH = gfx->mScreenHeight;

    Matrix4f ortho;
    gfx->setOrthogonal(ortho.mMtx, RectArea(0, 0, screenW, screenH));

    // Dim backdrop.
    gfx->setColour(Colour(0, 0, 0, 160), true);
    gfx->setAuxColour(Colour(0, 0, 0, 160));
    gfx->fillRectangle(RectArea(0, 0, screenW, screenH));

    const int panelX = 74;
    const int panelY = 64;
    const int panelW = screenW - 148;
    const int panelH = screenH - 116;
    const int px1 = panelX, py1 = panelY;
    const int px2 = panelX + panelW, py2 = panelY + panelH;
    const int radius = 26;
    const int headerH = 34;

    drawPikminPanel(gfx, px1, py1, panelW, panelH, radius);
    drawPikminHeader(gfx, px1, py1, panelW, "PC Settings");

    const char* modeNames[3] = { "Windowed", "Fullscreen", "Borderless" };

    // Modal video-confirm dialog.
    if (sVideoConfirmActive) {
        const Uint32 elapsed = SDL_GetTicks() - sVideoConfirmStartMs;
        const Uint32 remainMs = elapsed >= kVideoConfirmDurationMs
                                    ? 0u : kVideoConfirmDurationMs - elapsed;
        const int remainS = (int)((remainMs + 999) / 1000); // redondeo al alza

        int cy = py1 + headerH + 26;
        drawTextOutline(px1 + panelW / 2 - sFont->stringWidth("Video settings changed.") / 2, cy,
                        "Video settings changed.", Colour(255, 240, 180, 255), Colour(18, 26, 56, 255));
        drawTextOutline(px1 + panelW / 2 - sFont->stringWidth("A: keep   B: revert") / 2, cy + 26,
                        "A: keep   B: revert", Colour(255, 255, 255, 255), Colour(18, 26, 56, 255));
        char autoBuf[64];
        snprintf(autoBuf, sizeof(autoBuf), "Auto-reverting in %d s", remainS);
        drawTextOutline(px1 + panelW / 2 - sFont->stringWidth(autoBuf) / 2, cy + 52,
                        "%s", Colour(255, 255, 255, 255), Colour(18, 26, 56, 255), autoBuf);
        return;
    }

    const char* labels[ROW_COUNT] = {
        "Display Mode", "Resolution", "Aspect Ratio", "3D Resolution", "Refresh Rate", "Frame Sync (VSync)",
        "FPS Mode", "Controls", "Gamepad", "Advanced Settings", "Mods",
        "[  Reset to Defaults  ]", "[  Save  ]", "[  Close  ]",
    };
    const bool actionRow[ROW_COUNT] = { false, false, false, false, false, false, false, false, false, false, false, true, true, true };

    const char* aspectNames[5] = { "Auto", "4:3", "16:10", "16:9", "21:9" };
    char aspectBuf[32];
    snprintf(aspectBuf, sizeof(aspectBuf), "%s", aspectNames[sPending.aspectRatioMode >= 0 && sPending.aspectRatioMode < 5 ? sPending.aspectRatioMode : 0]);

    const char* fpsModeNames[2] = { "30 FPS (stable)", "60 FPS (experimental)" };
    char fpsModeBuf[32];
    snprintf(fpsModeBuf, sizeof(fpsModeBuf), "%s", fpsModeNames[sPending.fpsMode >= 0 && sPending.fpsMode < 2 ? sPending.fpsMode : 0]);

    char valueBuf[7][128];
    snprintf(valueBuf[0], sizeof(valueBuf[0]), "%s",
             modeNames[sPending.displayMode >= 0 && sPending.displayMode < 3 ? sPending.displayMode : 0]);
    if (sPending.displayMode == PC_WINDOW_FULLSCREEN_BORDERLESS) {
        if (sDesktopW > 0) {
            snprintf(valueBuf[1], sizeof(valueBuf[1]), "Desktop (%dx%d)", sDesktopW, sDesktopH);
        } else {
            snprintf(valueBuf[1], sizeof(valueBuf[1]), "Desktop");
        }
    } else {
        char aspectTag[16];
        aspectLabel(sPending.windowWidth, sPending.windowHeight, aspectTag, sizeof(aspectTag));
        const bool native = sPending.windowWidth == sDesktopW && sPending.windowHeight == sDesktopH;
        snprintf(valueBuf[1], sizeof(valueBuf[1]), "%dx%d  %s%s", sPending.windowWidth,
                 sPending.windowHeight, aspectTag, native ? "  (native)" : "");
    }
    snprintf(valueBuf[2], sizeof(valueBuf[2]), "%s", aspectBuf);
    {
        float rs = sPending.renderScale;
        if (fabsf(rs - 2.0f / 3.0f) < 0.01f) snprintf(valueBuf[3], sizeof(valueBuf[3]), "Auto (native)");
        else snprintf(valueBuf[3], sizeof(valueBuf[3]), "%.2fx", rs);
    }
    if (sPending.refreshRate <= 0.0) snprintf(valueBuf[4], sizeof(valueBuf[4]), "Auto");
    else snprintf(valueBuf[4], sizeof(valueBuf[4]), "%.0f Hz", sPending.refreshRate);
    snprintf(valueBuf[5], sizeof(valueBuf[5]), "%s", sPending.vsync ? "On" : "Off");
    snprintf(valueBuf[6], sizeof(valueBuf[6]), "%s", fpsModeBuf);

    // Only the first seven rows have computed setting values; the remaining
    // four open submenus. This table must stay the same length as the run of
    // non-action rows in `Row` -- indexing past it would print adjacent memory.
    const char* rowValues[ROW_RESET] = {
        valueBuf[0], valueBuf[1], valueBuf[2], valueBuf[3], valueBuf[4],
        valueBuf[5], valueBuf[6], "Open >", "Open >", "Open >", "Open >",
    };

    const int rowH = 18;
    const int labelRight = px1 + panelW / 2 - 12;
    const int valueLeft = px1 + panelW / 2 + 20;
    int y = py1 + headerH + 12;
    for (int i = 0; i < ROW_COUNT; i++) {
        bool selected = (i == sSelection);
        if (actionRow[i]) {
            int tx = px1 + panelW / 2 - sFont->stringWidth(labels[i]) / 2;
            if (selected) {
                fillRoundRectGrad(gfx, px1 + 70, y - 3, panelW - 140, rowH + 4, 8,
                                  Colour(47, 45, 35, 210), Colour(8, 8, 10, 220));
            }
            drawTextOutline(tx, y, "%s",
                            selected ? Colour(255, 190, 28, 255) : Colour(211, 246, 255, 255),
                            selected ? Colour(62, 25, 0, 255) : Colour(0, 10, 14, 255), labels[i]);
        } else {
            if (selected) {
                fillRoundRectGrad(gfx, px1 + 48, y - 3, panelW - 96, rowH + 4, 8,
                                  Colour(54, 49, 34, 210), Colour(8, 8, 10, 220));
            }
            Colour main = selected ? Colour(255, 190, 28, 255) : Colour(178, 235, 255, 255);
            Colour shadow = selected ? Colour(62, 25, 0, 255) : Colour(0, 10, 18, 255);
            drawTextOutline(labelRight - sFont->stringWidth(labels[i]), y, "%s",
                            main, shadow, labels[i]);
            drawTextOutline(valueLeft, y, "%s", main, shadow, rowValues[i]);
            if (selected) {
                drawTextOutline(px1 + 29, y, ">", Colour(255, 232, 130, 255),
                                Colour(45, 18, 0, 255));
            }
        }
        y += rowH;
    }

    // Controls submenu overlay.
    if (sInControlsSubmenu) {
        const int subX = px1 + 18, subY = py1 + 44;
        const int subW = panelW - 36, subH = panelH - 58;
        drawSubmenuSurface(gfx, subX, subY, subW, subH, "Keyboard Controls",
                           "Enter: capture   Left/Right: default",
                           "Up/Down: select   Esc/B: back");

        // List of actions.
        const int listStartY = subY + 48;
        const int itemH = 24;
        const int visibleItems = 9;
        int startIdx = 0;
        if (sControlSelection >= visibleItems) {
            startIdx = sControlSelection - visibleItems + 1;
        }
        int endIdx = startIdx + visibleItems;
        if (endIdx > PC_KEY_ACT_COUNT) endIdx = PC_KEY_ACT_COUNT;

        for (int i = startIdx; i < endIdx; i++) {
            int itemY = listStartY + (i - startIdx) * itemH;
            bool selected = (i == sControlSelection);
            bool waiting = sWaitingForKey && selected;

            const char* actionName = pc_window_get_key_action_name(i);
            SDL_Scancode boundSc = pc_window_get_key_binding(i);
            const char* scName = SDL_GetScancodeName(boundSc);

            char value[96];
            if (waiting) {
                snprintf(value, sizeof(value), "[Press a key...]");
            } else {
                snprintf(value, sizeof(value), "%s", scName ? scName : "None");
            }
            drawSubmenuRow(gfx, subX + 20, itemY, subW - 40,
                           actionName, value, selected);
        }

        // Scroll hint if more items exist.
        if (PC_KEY_ACT_COUNT > visibleItems) {
            char hint[64];
            snprintf(hint, sizeof(hint), "%d / %d", sControlSelection + 1, PC_KEY_ACT_COUNT);
            drawTextOutline(subX + subW - 12 - sFont->stringWidth(hint),
                            subY + 12, "%s",
                            Colour(180, 180, 200, 255), Colour(10, 16, 36, 255), hint);
        }

        return; // Don't draw footer when submenu is open.
    }

    // Gamepad submenu overlay.
    if (sInGamepadSubmenu) {
        const int subX = px1 + 18, subY = py1 + 44;
        const int subW = panelW - 36, subH = panelH - 58;
        drawSubmenuSurface(gfx, subX, subY, subW, subH, "Gamepad Controls",
                           "Enter: capture   Left/Right: default",
                           "Up/Down: select   Esc/B: back");

        const int listStartY = subY + 48;
        const int itemH = 24;
        const int visibleItems = 9;
        int startIdx = 0;
        if (sGamepadSelection >= visibleItems) {
            startIdx = sGamepadSelection - visibleItems + 1;
        }
        int endIdx = startIdx + visibleItems;
        if (endIdx > PC_KEY_ACT_COUNT) endIdx = PC_KEY_ACT_COUNT;

        for (int i = startIdx; i < endIdx; i++) {
            int itemY = listStartY + (i - startIdx) * itemH;
            bool selected = (i == sGamepadSelection);
            bool waiting = sWaitingForButton && selected;

            const char* actionName = pc_window_get_key_action_name(i);
            int boundBtn = sPending.gamepadBindings[i];
            if (boundBtn < 0) boundBtn = kDefaultGamepadBindings[i];
            const char* btnName = pc_window_get_gamepad_button_name(boundBtn);

            char value[96];
            if (waiting) {
                snprintf(value, sizeof(value), "[Press a button...]");
            } else {
                snprintf(value, sizeof(value), "%s", btnName ? btnName : "None");
            }
            drawSubmenuRow(gfx, subX + 20, itemY, subW - 40,
                           actionName, value, selected);
        }

        if (PC_KEY_ACT_COUNT > visibleItems) {
            char hint[64];
            snprintf(hint, sizeof(hint), "%d / %d", sGamepadSelection + 1, PC_KEY_ACT_COUNT);
            drawTextOutline(subX + subW - 12 - sFont->stringWidth(hint),
                            subY + 12, "%s",
                            Colour(180, 180, 200, 255), Colour(10, 16, 36, 255), hint);
        }

        return; // Don't draw footer when gamepad submenu is open.
    }

    // Advanced settings submenu overlay.
    if (sInAdvancedSubmenu) {
        const int subX = px1 + 18, subY = py1 + 44;
        const int subW = panelW - 36, subH = panelH - 58;
        drawSubmenuSurface(gfx, subX, subY, subW, subH, "Advanced Settings",
                           "Left/Right: adjust",
                           "Up/Down: select   Esc/B: back");

        const char* advancedLabels[kAdvancedRowCount] = {
            "Mouse Sensitivity",
            "Stick Dead Zone",
            "Stick Invert (X/Y)",
            "C-Stick Invert (X/Y)",
        };

        const int listStartY = subY + 62;
        const int itemH = 28;

        for (int i = 0; i < kAdvancedRowCount; i++) {
            int itemY = listStartY + i * itemH;
            bool selected = (i == sAdvancedSelection);

            char value[64];
            if (i == 0) {
                snprintf(value, sizeof(value), "%.2f", sPending.mouseSensitivity);
            } else if (i == 1) {
                snprintf(value, sizeof(value), "%d", sPending.stickDeadZone);
            } else if (i == 2) {
                snprintf(value, sizeof(value), "%s / %s",
                         (sPending.stickInvert & 1) ? "InvX" : "NorX",
                         (sPending.stickInvert & 2) ? "InvY" : "NorY");
            } else {
                snprintf(value, sizeof(value), "%s / %s",
                         (sPending.cStickInvert & 1) ? "InvX" : "NorX",
                         (sPending.cStickInvert & 2) ? "InvY" : "NorY");
            }
            drawSubmenuRow(gfx, subX + 20, itemY, subW - 40,
                           advancedLabels[i], value, selected);
        }

        return; // Don't draw footer when advanced submenu is open.
    }

    // Resolution submenu overlay.
    if (sInResolutionSubmenu) {
        const int subX = px1 + 18, subY = py1 + 44;
        const int subW = panelW - 36, subH = panelH - 58;
        drawSubmenuSurface(gfx, subX, subY, subW, subH, "Resolution",
                           "Enter: apply",
                           "Up/Down: select   Esc/B: back");

        const int listStartY = subY + 48;
        const int itemH = 24;
        const int visibleItems = 9;
        const int choiceCount = (int)sResolutionChoices.size();
        int startIdx = 0;
        if (sResolutionSubmenuSel >= visibleItems) {
            startIdx = sResolutionSubmenuSel - visibleItems + 1;
        }
        int endIdx = startIdx + visibleItems;
        if (endIdx > choiceCount) endIdx = choiceCount;

        for (int k = startIdx; k < endIdx; k++) {
            const Resolution& r = sResolutions[sResolutionChoices[k]];
            const int itemY = listStartY + (k - startIdx) * itemH;
            const bool selected = (k == sResolutionSubmenuSel);
            const bool current = r.w == sPending.windowWidth && r.h == sPending.windowHeight;

            char label[64];
            snprintf(label, sizeof(label), "%s%dx%d", current ? "> " : "  ", r.w, r.h);

            char aspectTag[16];
            aspectLabel(r.w, r.h, aspectTag, sizeof(aspectTag));
            char value[96];
            snprintf(value, sizeof(value), "%s%s", aspectTag,
                     r.isNative ? "  (native)" : (r.isDerived ? "  (window)" : ""));

            drawSubmenuRow(gfx, subX + 20, itemY, subW - 40, label, value, selected);
        }

        if (choiceCount > visibleItems) {
            char hint[64];
            snprintf(hint, sizeof(hint), "%d / %d", sResolutionSubmenuSel + 1, choiceCount);
            drawTextOutline(subX + subW - 12 - sFont->stringWidth(hint),
                            subY + 12, "%s",
                            Colour(180, 180, 200, 255), Colour(10, 16, 36, 255), hint);
        }

        return; // Don't draw footer when submenu is open.
    }

    // Mods submenu overlay.
    if (sInModsSubmenu) {
        const int subX = px1 + 18, subY = py1 + 44;
        const int subW = panelW - 36, subH = panelH - 58;
        drawSubmenuSurface(gfx, subX, subY, subW, subH, "Mods",
                           "Left/Right: change   These change how the game plays",
                           "Up/Down: select   Esc/B: back");

        const char* modsLabels[kModsRowCount] = {
            "Control Scheme",
            "Chain Pikmin Actions",
        };

        const int listStartY = subY + 62;
        const int itemH = 28;

        for (int i = 0; i < kModsRowCount; i++) {
            int itemY = listStartY + i * itemH;
            bool selected = (i == sModsSelection);

            char value[64];
            if (i == 0) {
                snprintf(value, sizeof(value), "%s",
                         sPending.controlMode == PC_CONTROL_CLASSIC ? "Classic (original)"
                                                                    : "Mouse Cursor");
            } else {
                snprintf(value, sizeof(value), "%s",
                         sPending.chainActions ? "On" : "Off (original)");
            }
            drawSubmenuRow(gfx, subX + 20, itemY, subW - 40,
                           modsLabels[i], value, selected);
        }

        return; // Don't draw footer when mods submenu is open.
    }

    // Footer / help.
    drawTextOutline(px1 + panelW / 2 - sFont->stringWidth("Left/Right: change   Up/Down: move   Esc: close") / 2, y + 14,
                    "Left/Right: change   Up/Down: move   Esc: close",
                    Colour(200, 210, 235, 255), Colour(10, 16, 36, 255));
    if (pc_window_get_last_error()[0]) {
        char errBuf[128];
        snprintf(errBuf, sizeof(errBuf), "Video error: %s", pc_window_get_last_error());
        drawTextOutline(px1 + panelW / 2 - sFont->stringWidth(errBuf) / 2, y + 34,
                        "%s", Colour(255, 120, 120, 255), Colour(10, 16, 36, 255), errBuf);
    }
}

int pc_settings_get_fps_mode(void) {
    return sConfig.fpsMode;
}

int pc_settings_get_chain_actions(void) {
    return sConfig.chainActions;
}
