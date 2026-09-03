#include "gamecube_image.h"
#include "installer_ui.h"

#include <cerrno>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using pikmin::launcher::DiscIdentity;

namespace {

fs::path defaultDataRoot()
{
    if (const char* xdg = std::getenv("XDG_DATA_HOME")) return fs::path(xdg) / "pikmin-native";
    if (const char* home = std::getenv("HOME")) return fs::path(home) / ".local/share/pikmin-native";
    return fs::current_path() / "pikmin-native-data";
}

fs::path executablePath()
{
    if (const char* env = std::getenv("PIKMIN_EXECUTABLE_PATH")) {
        return fs::path(env);
    }
    std::vector<char> path(4096);
    const ssize_t count = readlink("/proc/self/exe", path.data(), path.size() - 1);
    if (count <= 0) return {};
    path[static_cast<std::size_t>(count)] = '\0';
    return fs::path(path.data());
}

fs::path executableDirectory()
{
    const fs::path path = executablePath();
    if (path.empty()) return fs::current_path();
    return path.parent_path();
}

bool commandExists(const char* command)
{
    const char* pathValue = std::getenv("PATH");
    if (!pathValue) return false;
    std::string paths(pathValue);
    std::size_t start = 0;
    while (start <= paths.size()) {
        const std::size_t end = paths.find(':', start);
        const fs::path candidate = fs::path(paths.substr(start, end - start)) / command;
        if (access(candidate.c_str(), X_OK) == 0) return true;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

std::string runDialog(const char* program, const std::vector<std::string>& args)
{
    int pipeFds[2];
    if (pipe(pipeFds) != 0) return {};
    const pid_t child = fork();
    if (child == 0) {
        dup2(pipeFds[1], STDOUT_FILENO);
        // GTK/libadwaita diagnostics and unsupported cosmetic Zenity options
        // belong to the dialog process. Do not mix them with the launcher's
        // extraction progress or with the selected path read from stdout.
        const int nullFd = open("/dev/null", O_WRONLY);
        if (nullFd >= 0) {
            dup2(nullFd, STDERR_FILENO);
            close(nullFd);
        }
        close(pipeFds[0]); close(pipeFds[1]);
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(program));
        for (const std::string& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        execvp(program, argv.data());
        _exit(127);
    }
    close(pipeFds[1]);
    std::string result;
    char buffer[1024];
    ssize_t count;
    while ((count = read(pipeFds[0], buffer, sizeof(buffer))) > 0) result.append(buffer, count);
    close(pipeFds[0]);
    int status = 0;
    waitpid(child, &status, 0);
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) result.pop_back();
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return {};
    return result;
}

bool hasGraphicalDialogs()
{
    return commandExists("zenity") || commandExists("kdialog");
}

bool stdinIsTerminal()
{
    return isatty(STDIN_FILENO) != 0;
}

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

// When the launcher is started by double-clicking it in a file manager there
// is no terminal to show errors on. Re-run itself inside a terminal emulator
// so the text-mode installer and any error message become visible.
bool respawnInTerminal()
{
    if (std::getenv("PIKMIN_LAUNCHER_TERMINAL")) return false;
    const fs::path self = executablePath();
    if (self.empty()) return false;
    static const char* const terminals[] = {
        "x-terminal-emulator", "gnome-terminal", "konsole",
        "xfce4-terminal", "mate-terminal", "lxterminal", "xterm"
    };
    for (const char* terminal : terminals) {
        if (!commandExists(terminal)) continue;
        const pid_t child = fork();
        if (child == 0) {
            setenv("PIKMIN_LAUNCHER_TERMINAL", "1", 1);
            execlp(terminal, terminal, "-e", self.c_str(),
                   static_cast<char*>(nullptr));
            _exit(127);
        }
        if (child > 0) {
            int status = 0;
            waitpid(child, &status, 0);
            return true;
        }
    }
    return false;
}

void showMessage(const std::string& title, const std::string& message, bool error = false)
{
    if (commandExists("zenity")) {
        runDialog("zenity", { error ? "--error" : "--info", "--title=" + title, "--text=" + message,
                               "--width=480" });
        return;
    }
    else if (commandExists("kdialog")) {
        runDialog("kdialog", { error ? "--error" : "--msgbox", message, "--title", title });
        return;
    }
    std::cerr << title << ": " << message << '\n';
}

fs::path askForInstallDirectory()
{
    if (commandExists("zenity")) {
        std::string initial;
        if (const char* home = std::getenv("HOME")) initial = fs::path(home).string() + "/";
        const std::string selected = runDialog("zenity", {
            "--file-selection", "--directory",
            "--title=Pikmin Native - Selecciona la carpeta de instalación",
            "--filename=" + initial
        });
        if (!selected.empty()) return selected;
    } else if (commandExists("kdialog")) {
        const std::string initial = std::getenv("HOME") ? std::getenv("HOME") : ".";
        const std::string selected = runDialog("kdialog", {
            "--getexistingdirectory", initial,
            "--title", "Pikmin Native - Selecciona la carpeta de instalación"
        });
        if (!selected.empty()) return selected;
    }
    return {};
}

fs::path askForImage()
{
    if (commandExists("zenity")) {
        const std::string selected = runDialog("zenity", {
            "--file-selection", "--title=Pikmin Native - Selecciona tu disco",
            "--file-filter=GameCube ISO/GCM | *.iso *.ISO *.gcm *.GCM",
            "--file-filter=Todos los archivos | *"
        });
        if (!selected.empty()) return selected;
    }
    else if (commandExists("kdialog")) {
        const std::string selected = runDialog("kdialog", {
            "--getopenfilename", ".", "*.iso *.ISO *.gcm *.GCM|GameCube ISO/GCM"
        });
        if (!selected.empty()) return selected;
    }
    return {};
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
    const fs::path partialAssets = dataRoot / ("assets.partial." + std::to_string(getpid()));
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
    const fs::path sourceGame = sourceDirectory / "pikmin";
    const fs::path sourceLauncher = sourceDirectory / "pikmin-launcher";
    const fs::path sourceGameReal = sourceDirectory / "pikmin.real";
    const fs::path sourceLauncherReal = sourceDirectory / "pikmin-launcher.real";
    const fs::path sourceLib = sourceDirectory / "lib";

    const bool isStandalone = fs::is_regular_file(sourceGameReal)
                            && fs::is_regular_file(sourceLauncherReal)
                            && fs::is_directory(sourceLib);

    if (!isStandalone && (!fs::is_regular_file(sourceGame) || !fs::is_regular_file(sourceLauncher))) {
        failure = "El paquete está incompleto: pikmin y pikmin-launcher deben estar juntos.";
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
        if (!makeWrapper("pikmin") || !makeWrapper("pikmin-launcher")) {
            failure = "No se pudieron crear los lanzadores.";
            return false;
        }
        return true;
    }

    for (const char* name : { "pikmin", "pikmin-launcher" }) {
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
}

[[noreturn]] void launchGame(const fs::path& dataRoot, const fs::path& gameBinary)
{
    if (chdir(dataRoot.c_str()) != 0) {
        std::cerr << "No se pudo entrar en " << dataRoot << ": " << std::strerror(errno) << '\n';
        std::exit(1);
    }
    execl(gameBinary.c_str(), gameBinary.c_str(), static_cast<char*>(nullptr));
    std::cerr << "No se pudo iniciar " << gameBinary << ": " << std::strerror(errno) << '\n';
    std::exit(1);
}

void usage(const char* argv0)
{
    std::cout << "Uso: " << argv0 << " [--rom ARCHIVO.iso] [--install-dir DIR] [--extract-only]\n"
              << "Sin argumentos abre el instalador gráfico (necesita zenity o kdialog);\n"
              << "desde una terminal sin ellos se usa el instalador en modo texto.\n"
              << "La ROM debe proceder de una copia legítima de Pikmin USA Rev. 1.\n";
}

} // namespace

int main(int argc, char** argv)
{
    fs::path image;
    fs::path dataRoot;
    bool extractOnly = false;
    bool directoryWasSpecified = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--rom" && i + 1 < argc) image = argv[++i];
        else if ((arg == "--install-dir" || arg == "--data-dir") && i + 1 < argc) {
            dataRoot = argv[++i];
            directoryWasSpecified = true;
        }
        else if (arg == "--extract-only") extractOnly = true;
        else if (arg == "--help" || arg == "-h") { usage(argv[0]); return 0; }
        else { usage(argv[0]); return 2; }
    }

    const fs::path sourceDirectory = executableDirectory();
    const bool hasStandaloneFiles = fs::is_regular_file(sourceDirectory / "pikmin.real")
                                 && fs::is_regular_file(sourceDirectory / "pikmin-launcher.real")
                                 && fs::is_directory(sourceDirectory / "lib");
    const bool installedBesideLauncher = assetsReady(sourceDirectory)
                                      && (fs::is_regular_file(sourceDirectory / "pikmin") || hasStandaloneFiles);
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
        std::string failure;
        const auto progress = [&installerWindow](std::uint32_t percent, const std::string& path) {
            if (installerWindow) installerWindow->updateProgress(percent, path);
        };
        if (!installAssets(image, dataRoot, failure, progress)) {
            reportError(failure);
            return 1;
        }
        installedAssetsNow = true;
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

    const fs::path gameBinary = dataRoot / "pikmin";
    if (!fs::is_regular_file(gameBinary)) {
        std::cerr << "No se encontró el ejecutable del juego junto al launcher: " << gameBinary << '\n';
        return 1;
    }
    installerWindow.reset();
    launchGame(dataRoot, gameBinary);
}
