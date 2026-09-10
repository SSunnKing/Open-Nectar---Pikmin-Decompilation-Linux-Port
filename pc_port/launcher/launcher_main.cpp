#include "gamecube_image.h"
#include "installer_ui.h"
#include "launcher_platform.h"

#include <cerrno>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using pikmin::launcher::DiscIdentity;

namespace {

namespace platform = pikmin::launcher::platform;

// Windows exige la extensión .exe; el resto de sistemas usan el nombre pelado.
#ifdef _WIN32
constexpr const char* kGameExecutable     = "nectar.exe";
constexpr const char* kLauncherExecutable = "nectar-launcher.exe";
#else
constexpr const char* kGameExecutable     = "nectar";
constexpr const char* kLauncherExecutable = "nectar-launcher";
#endif


// Reenvíos a la capa de plataforma. Las implementaciones concretas viven en
// launcher_platform_posix.cpp y launcher_platform_win32.cpp.
fs::path defaultDataRoot() { return platform::defaultDataRoot(); }
fs::path executableDirectory()
{
    const fs::path path = platform::executablePath();
    if (path.empty()) return fs::current_path();
    return path.parent_path();
}
bool hasGraphicalDialogs() { return platform::hasGraphicalDialogs(); }
bool stdinIsTerminal() { return platform::stdinIsTerminal(); }

std::string trim(const std::string& value)
{
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string promptLine(const std::string& prompt)
{
    std::cout << prompt << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) return {};
    return trim(line);
}

bool respawnInTerminal() { return platform::respawnInTerminal(); }

fs::path askForInstallDirectory() { return platform::askForInstallDirectory(); }
fs::path askForImage() { return platform::askForImage(); }
int askForLanguage(const std::vector<std::string>& names) { return platform::askForLanguage(names); }

// Writes one key into the game's settings file without disturbing the rest.
//
// The file belongs to the game, which rewrites it whole every time it saves,
// so the key has to be one the game knows -- it is; see
// pc_settings_startup_language. This only sets the initial value.
bool writeSettingKey(const fs::path& dataRoot, const std::string& key, const std::string& value)
{
    const fs::path path = dataRoot / "pikmin_settings.conf";
    std::vector<std::string> lines;
    bool replaced = false;
    {
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) {
            const std::size_t equals = line.find('=');
            if (equals != std::string::npos && trim(line.substr(0, equals)) == key) {
                lines.push_back(key + " = " + value);
                replaced = true;
            } else {
                lines.push_back(line);
            }
        }
    }
    if (!replaced) {
        if (lines.empty()) lines.push_back("# Open Nectar settings (F1 in-game to change)");
        lines.push_back(key + " = " + value);
    }
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    for (const std::string& line : lines) out << line << '\n';
    return true;
}

// The languages a disc carries, with names to show and the codes the settings
// file uses.
struct LanguageChoice {
    const char* code;
    const char* name;
};
const LanguageChoice* languageChoice(const std::string& code)
{
    static const LanguageChoice kChoices[] = {
        { "en", "English" }, { "de", "Deutsch" }, { "fr", "Français" },
        { "es", "Español" }, { "it", "Italiano" }, { "nl", "Nederlands" },
    };
    for (const LanguageChoice& choice : kChoices) {
        if (code == choice.code) return &choice;
    }
    return nullptr;
}

fs::path askForImageConsole()
{
    std::cout << "Instalador en modo texto (sin Zenity/KDialog).\n";
    return promptLine("Ruta de la imagen ISO/GCM de Pikmin USA Rev. 1: ");
}

fs::path askForInstallDirectoryConsole()
{
    const fs::path fallback = defaultDataRoot();
    const std::string selected = promptLine(
        "Carpeta de instalación [" + fallback.string() + "]: ");
    return selected.empty() ? fallback : fs::path(selected);
}

bool assetsReady(const fs::path& dataRoot)
{
    return fs::is_regular_file(dataRoot / "assets/.pikmin-assets")
        && fs::is_directory(dataRoot / "assets/dataDir")
        && fs::is_regular_file(dataRoot / "assets/dataDir/parms/gamePrms.bin");
}

std::string lowerExtension(const fs::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

bool installAssets(const fs::path& image, const fs::path& dataRoot, std::string& failure,
                   const std::function<void(std::uint32_t, const std::string&)>& progressCallback)
{
    DiscIdentity identity;
    std::string error;
    if (!pikmin::launcher::inspectGameCubeImage(image, identity, error)
        || !pikmin::launcher::isSupportedPikminDisc(identity, error)) {
        failure = error;
        return false;
    }

    const fs::path finalAssets = dataRoot / "assets";
    const fs::path partialAssets = dataRoot / ("assets.partial." + std::to_string(platform::currentProcessId()));
    std::error_code ec;
    fs::create_directories(dataRoot, ec);
    if (ec) {
        failure = "No se pudo crear el directorio de instalación: " + ec.message();
        return false;
    }
    if (fs::exists(partialAssets)) {
        failure = "Ya existe una extracción temporal en " + partialAssets.string()
                + ". Elimínala manualmente si ya no está en uso.";
        return false;
    }
    std::cout << "Extrayendo los datos del disco. La ROM no se copia ni se modifica...\n";
    std::uint32_t lastPercent = 101;
    const bool extracted = pikmin::launcher::extractGameCubeImage(
        image, partialAssets, error,
        [&](std::uint32_t current, std::uint32_t total, const std::string& path) {
            const std::uint32_t percent = total ? current * 100 / total : 100;
            if (percent != lastPercent) {
                std::cout << "\r[" << percent << "%] " << path << "          " << std::flush;
                if (progressCallback) progressCallback(percent, path);
                lastPercent = percent;
            }
        });
    std::cout << '\n';
    if (!extracted) {
        fs::remove_all(partialAssets, ec);
        failure = "Error de extracción: " + error;
        return false;
    }
    if (!fs::is_regular_file(partialAssets / "dataDir/parms/gamePrms.bin")) {
        fs::remove_all(partialAssets, ec);
        failure = "La imagen no contiene el árbol dataDir esperado.";
        return false;
    }
    std::ofstream marker(partialAssets / ".pikmin-assets", std::ios::trunc);
    marker << identity.gameId << " revision=" << unsigned(identity.revision) << '\n';
    marker.close();
    if (fs::exists(finalAssets)) {
        failure = "Ya existe un directorio de assets en " + finalAssets.string()
                + ". No se sobrescribirá automáticamente.";
        fs::remove_all(partialAssets, ec);
        return false;
    }
    fs::rename(partialAssets, finalAssets, ec);
    if (ec) {
        failure = "No se pudo finalizar la instalación: " + ec.message();
        return false;
    }
    std::cout << "Assets instalados en " << finalAssets << "\n";
    return true;
}

bool sameFile(const fs::path& lhs, const fs::path& rhs)
{
    std::error_code ec;
    return fs::exists(lhs) && fs::exists(rhs) && fs::equivalent(lhs, rhs, ec) && !ec;
}

bool installExecutables(const fs::path& sourceDirectory, const fs::path& installDirectory,
                        std::string& failure)
{
#ifdef _WIN32
    // En Windows el paquete no lleva cargador ni envoltorios: junto a los .exe
    // viajan las DLL (SDL2 y las del runtime), y basta con copiarlo todo.
    std::error_code winEc;
    fs::create_directories(installDirectory, winEc);
    if (winEc) {
        failure = "No se pudo crear la carpeta de instalación: " + winEc.message();
        return false;
    }
    for (const char* name : { kGameExecutable, kLauncherExecutable }) {
        const fs::path source = sourceDirectory / name;
        if (!fs::is_regular_file(source)) {
            failure = "El paquete está incompleto: falta " + std::string(name) + ".";
            return false;
        }
        const fs::path destination = installDirectory / name;
        if (sameFile(source, destination)) continue;
        fs::copy_file(source, destination, fs::copy_options::overwrite_existing, winEc);
        if (winEc) {
            failure = "No se pudo instalar " + std::string(name) + ": " + winEc.message();
            return false;
        }
    }
    for (const auto& entry : fs::directory_iterator(sourceDirectory, winEc)) {
        if (!entry.is_regular_file()) continue;
        std::string extension = entry.path().extension().string();
        for (char& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (extension != ".dll") continue;
        const fs::path destination = installDirectory / entry.path().filename();
        if (sameFile(entry.path(), destination)) continue;
        fs::copy_file(entry.path(), destination, fs::copy_options::overwrite_existing, winEc);
        if (winEc) {
            failure = "No se pudo copiar " + entry.path().filename().string() + ": " + winEc.message();
            return false;
        }
    }
    return true;
#else
    const fs::path sourceGame = sourceDirectory / kGameExecutable;
    const fs::path sourceLauncher = sourceDirectory / kLauncherExecutable;
    const fs::path sourceGameReal = sourceDirectory / (std::string(kGameExecutable) + ".real");
    const fs::path sourceLauncherReal
        = sourceDirectory / (std::string(kLauncherExecutable) + ".real");
    const fs::path sourceLib = sourceDirectory / "lib";

    const bool isStandalone = fs::is_regular_file(sourceGameReal)
                            && fs::is_regular_file(sourceLauncherReal)
                            && fs::is_directory(sourceLib);

    if (!isStandalone && (!fs::is_regular_file(sourceGame) || !fs::is_regular_file(sourceLauncher))) {
        failure = "El paquete está incompleto: " + std::string(kGameExecutable) + " y "
                + kLauncherExecutable + " deben estar juntos.";
        return false;
    }

    std::error_code ec;
    fs::create_directories(installDirectory, ec);
    if (ec) {
        failure = "No se pudo crear la carpeta de instalación: " + ec.message();
        return false;
    }

    if (isStandalone) {
        for (const auto& entry : { sourceGameReal, sourceLauncherReal }) {
            const fs::path dest = installDirectory / entry.filename();
            if (!sameFile(entry, dest)) {
                fs::copy_file(entry, dest, fs::copy_options::overwrite_existing, ec);
                if (ec) {
                    failure = "No se pudo instalar " + entry.filename().string() + ": " + ec.message();
                    return false;
                }
            }
            fs::permissions(dest, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                            fs::perm_options::add, ec);
            if (ec) {
                failure = "No se pudo hacer ejecutable " + dest.string() + ": " + ec.message();
                return false;
            }
        }
        const fs::path destLib = installDirectory / "lib";
        std::error_code eqEc;
        const bool libAlreadyInstalled = fs::exists(destLib)
                                      && fs::equivalent(sourceLib, destLib, eqEc) && !eqEc;
        if (!libAlreadyInstalled) {
            if (fs::exists(destLib)) {
                const fs::path destLoader = destLib / "ld-linux-x86-64.so.2";
                const fs::path sourceLoader = sourceLib / "ld-linux-x86-64.so.2";
                if (!fs::is_regular_file(destLoader) || !fs::is_regular_file(sourceLoader)) {
                    failure = "El directorio lib existente no pertenece a Nectar. "
                              "Elige otra carpeta de instalación o elimina manualmente " + destLib.string();
                    return false;
                }
                fs::remove_all(destLib, ec);
                if (ec) {
                    failure = "No se pudo limpiar el directorio lib anterior: " + ec.message();
                    return false;
                }
            }
            fs::copy(sourceLib, destLib, fs::copy_options::recursive, ec);
            if (ec) {
                failure = "No se pudo copiar el directorio lib: " + ec.message();
                return false;
            }
        }

        auto makeWrapper = [&installDirectory](const char* name) -> bool {
            const fs::path wrapper = installDirectory / name;
            const std::string realName = std::string(name) + ".real";
            std::ofstream out(wrapper, std::ios::trunc);
            if (!out) return false;
            // Resuelve el directorio real aunque se invoque mediante PATH o un
            // enlace simbólico; el paquete puede moverse de sitio libremente.
            out << "#!/bin/sh\n"
                << "self=$0\n"
                << "case \"$self\" in */*) ;; *) self=$(command -v -- \"$self\" 2>/dev/null || printf '%s' \"$self\");; esac\n"
                << "if command -v readlink >/dev/null 2>&1; then self=$(readlink -f \"$self\"); fi\n"
                << "here=$(CDPATH= cd -- \"$(dirname -- \"$self\")\" && pwd)\n"
                << "export PIKMIN_EXECUTABLE_PATH=\"$here/" << realName << "\"\n"
                << "exec \"$here/lib/ld-linux-x86-64.so.2\" --library-path \"$here/lib\" \"$here/" << realName << "\" \"$@\"\n";
            out.close();
            if (!out) return false;
            std::error_code permEc;
            fs::permissions(wrapper, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                            fs::perm_options::add, permEc);
            return !permEc;
        };
        if (!makeWrapper(kGameExecutable) || !makeWrapper(kLauncherExecutable)) {
            failure = "No se pudieron crear los lanzadores.";
            return false;
        }
        return true;
    }

    for (const char* name : { kGameExecutable, kLauncherExecutable }) {
        const fs::path source = sourceDirectory / name;
        const fs::path destination = installDirectory / name;
        if (!sameFile(source, destination)) {
            fs::copy_file(source, destination, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                failure = "No se pudo instalar " + std::string(name) + ": " + ec.message();
                return false;
            }
        }
        fs::permissions(destination,
                        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                        fs::perm_options::add, ec);
        if (ec) {
            failure = "No se pudo hacer ejecutable " + destination.string() + ": " + ec.message();
            return false;
        }
    }
    return true;
#endif
}

[[noreturn]] void launchGame(const fs::path& dataRoot, const fs::path& gameBinary)
{
    platform::launchGame(dataRoot, gameBinary);
}

void usage(const char* argv0)
{
    std::cout << "Uso: " << argv0 << " [--rom ARCHIVO.iso] [--install-dir DIR] [--extract-only]\n"
              << "Sin argumentos abre el instalador gráfico (necesita zenity o kdialog);\n"
              << "desde una terminal sin ellos se usa el instalador en modo texto.\n"
              << "La ROM debe proceder de una copia legítima de Pikmin USA Rev. 1.\n"
              << "--skip-verify omite la comprobación de integridad de la imagen.\n";
}

} // namespace

int main(int argc, char** argv)
{
    fs::path image;
    fs::path dataRoot;
    bool extractOnly = false;
    bool skipVerify = false;
    bool directoryWasSpecified = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--rom" && i + 1 < argc) image = argv[++i];
        else if ((arg == "--install-dir" || arg == "--data-dir") && i + 1 < argc) {
            dataRoot = argv[++i];
            directoryWasSpecified = true;
        }
        else if (arg == "--extract-only") extractOnly = true;
        else if (arg == "--skip-verify") skipVerify = true;
        else if (arg == "--help" || arg == "-h") { usage(argv[0]); return 0; }
        else { usage(argv[0]); return 2; }
    }

    const fs::path sourceDirectory = executableDirectory();
    const bool hasStandaloneFiles
        = fs::is_regular_file(sourceDirectory / (std::string(kGameExecutable) + ".real"))
       && fs::is_regular_file(sourceDirectory / (std::string(kLauncherExecutable) + ".real"))
       && fs::is_directory(sourceDirectory / "lib");
    const bool installedBesideLauncher = assetsReady(sourceDirectory)
                                      && (fs::is_regular_file(sourceDirectory / kGameExecutable) || hasStandaloneFiles);
    const bool graphicalInstall = !directoryWasSpecified && !installedBesideLauncher;
    std::unique_ptr<pikmin::launcher::InstallerWindow> installerWindow;
    const auto reportError = [&installerWindow](const std::string& error) {
        if (installerWindow) installerWindow->showError(error);
        else std::cerr << "Error de instalación: " << error << '\n';
    };
    if (installedBesideLauncher) {
        dataRoot = sourceDirectory;
    } else if (graphicalInstall) {
        if (hasGraphicalDialogs()) {
            installerWindow = std::make_unique<pikmin::launcher::InstallerWindow>();
            std::string error;
            if (!installerWindow->open(error)) {
                std::cerr << "No se pudo abrir el instalador: " << error << '\n';
                return 1;
            }
            std::string selectedRom;
            std::string selectedInstallDirectory;
            if (!installerWindow->choosePaths(
                    [] { return askForImage().string(); },
                    [] { return askForInstallDirectory().string(); },
                    selectedRom, selectedInstallDirectory)) {
                return 0;
            }
            image = selectedRom;
            dataRoot = selectedInstallDirectory;
        } else if (stdinIsTerminal()) {
            image = askForImageConsole();
            if (image.empty()) {
                std::cerr << "No se seleccionó ninguna imagen.\n";
                return 1;
            }
            dataRoot = askForInstallDirectoryConsole();
        } else {
            if (respawnInTerminal()) return 0;
            std::cerr << "Se necesita Zenity o KDialog para el instalador gráfico.\n"
                         "Instala zenity (Debian/Ubuntu: sudo apt install zenity; Arch: sudo pacman -S zenity)\n"
                         "o ejecuta pikmin-launcher desde una terminal para usar el modo texto.\n";
            return 1;
        }
    } else if (dataRoot.empty()) {
        dataRoot = defaultDataRoot();
    }

    bool installedAssetsNow = false;
    if (!assetsReady(dataRoot)) {
        if (image.empty() && hasGraphicalDialogs()) image = askForImage();
        if (image.empty() && stdinIsTerminal()) image = askForImageConsole();
        if (image.empty()) {
            if (installerWindow) return 0;
            std::cerr << "No se seleccionó ninguna imagen.\n";
            return 1;
        }
        const std::string ext = lowerExtension(image);
        if (ext != ".iso" && ext != ".gcm") {
            const std::string error = "El instalador acepta ISO/GCM. Convierte RVZ/WIA/GCZ a ISO con dolphin-tool.";
            reportError(error);
            return 1;
        }
        // Comprobar la imagen antes de extraer: evita instalar durante minutos
        // desde una copia dañada y que el fallo aparezca mucho después, ya en
        // el juego, como un error incomprensible.
        if (!skipVerify) {
            if (installerWindow) installerWindow->updateProgress(0, "Verificando la imagen...");
            else std::cout << "Verificando la integridad de la imagen..." << std::flush;
            std::string verifyError;
            const auto verifyProgress = [&installerWindow](std::uint32_t percent) {
                if (installerWindow) {
                    installerWindow->updateProgress(percent, "Verificando la imagen...");
                }
            };
            if (!pikmin::launcher::verifyImageIntegrity(image, verifyError, verifyProgress)) {
                if (!installerWindow) std::cout << '\n';
                reportError(verifyError);
                return 1;
            }
            if (!installerWindow) std::cout << " correcta.\n";
        }

        std::string failure;
        const auto progress = [&installerWindow](std::uint32_t percent, const std::string& path) {
            if (installerWindow) installerWindow->updateProgress(percent, path);
        };
        if (!installAssets(image, dataRoot, failure, progress)) {
            reportError(failure);
            return 1;
        }
        installedAssetsNow = true;

        // Which language to play in. Only the European disc carries more than
        // one, and all of them are installed either way -- about 6 MB each out
        // of 648 MB, so leaving some out saves nothing and would mean
        // reinstalling to change your mind.
        pikmin::launcher::DiscIdentity identity;
        std::string ignored;
        const pikmin::launcher::KnownDisc* disc
            = pikmin::launcher::inspectGameCubeImage(image, identity, ignored)
                  ? pikmin::launcher::findKnownDisc(identity)
                  : nullptr;
        if (disc != nullptr && disc->languageCount > 1) {
            std::vector<std::string> names;
            for (int i = 0; i < disc->languageCount; ++i) {
                const LanguageChoice* choice = languageChoice(disc->languages[i]);
                names.push_back(choice ? choice->name : disc->languages[i]);
            }

            int selected = -1;
            if (stdinIsTerminal()) {
                std::cout << "\nEste disco trae " << disc->languageCount << " idiomas:\n";
                for (std::size_t i = 0; i < names.size(); ++i) {
                    std::cout << "  " << (i + 1) << ") " << names[i] << '\n';
                }
                const std::string answer = promptLine("¿En cuál quieres jugar? [1]: ");
                const int number = answer.empty() ? 1 : std::atoi(answer.c_str());
                if (number >= 1 && number <= disc->languageCount) selected = number - 1;
            } else {
                selected = askForLanguage(names);
            }

            // No way to ask, or nothing chosen: English, and say where to
            // change it rather than leaving it a mystery.
            const int language = (selected >= 0) ? selected : 0;
            if (writeSettingKey(dataRoot, "language", disc->languages[language])) {
                std::cout << "Idioma: " << names[language]
                          << "  (cámbialo en pikmin_settings.conf, clave 'language')\n";
            }
        }
    }

    std::string failure;
    if (!installExecutables(sourceDirectory, dataRoot, failure)) {
        reportError(failure);
        return 1;
    }
    if (installedAssetsNow) {
        if (installerWindow) installerWindow->showComplete(dataRoot.string(), !extractOnly);
        else std::cout << "Instalación completada en: " << dataRoot << '\n'
                       << (extractOnly ? "" : "El juego se iniciará ahora.\n");
    }
    if (extractOnly) return 0;

    const fs::path gameBinary = dataRoot / kGameExecutable;
    if (!fs::is_regular_file(gameBinary)) {
        std::cerr << "No se encontró el ejecutable del juego junto al launcher: " << gameBinary << '\n';
        return 1;
    }
    installerWindow.reset();
    launchGame(dataRoot, gameBinary);
}
