#ifndef PIKMIN_LAUNCHER_PLATFORM_H
#define PIKMIN_LAUNCHER_PLATFORM_H

// Operaciones del launcher que dependen del sistema operativo.
//
// launcher_main.cpp no incluye cabeceras de plataforma: todo lo específico
// vive detrás de esta interfaz, con una implementación por sistema:
//
//   launcher_platform_posix.cpp  → Linux/BSD, diálogos vía zenity o kdialog
//   launcher_platform_win32.cpp  → Windows, diálogos nativos del shell
//
// Las rutas se manejan siempre como std::filesystem::path, que en Windows
// guarda wchar_t internamente, de modo que los nombres con acentos o
// caracteres no ASCII sobreviven en ambos sistemas.

#include <filesystem>
#include <string>

namespace pikmin {
namespace launcher {
namespace platform {

// Ruta absoluta del ejecutable actual. Devuelve una ruta vacía si no se puede
// determinar. Respeta NECTAR_EXECUTABLE_PATH (y PIKMIN_EXECUTABLE_PATH, el
// nombre antiguo), que los envoltorios del paquete
// autocontenido usan para señalar el binario real.
std::filesystem::path executablePath();

// Carpeta de datos por defecto cuando el usuario no indica ninguna.
std::filesystem::path defaultDataRoot();

// True si hay diálogos gráficos disponibles. En Windows siempre, porque son
// parte del sistema; en POSIX depende de que exista zenity o kdialog.
bool hasGraphicalDialogs();

// True si la entrada estándar es una terminal interactiva, es decir, si tiene
// sentido usar el instalador en modo texto.
bool stdinIsTerminal();

// Identificador del proceso actual. Solo se usa para dar un nombre único a la
// carpeta temporal de extracción, de modo que dos instalaciones simultáneas no
// se pisen.
unsigned long currentProcessId();

// Diálogo de selección de la imagen ISO/GCM. Ruta vacía si se cancela.
std::filesystem::path askForImage();

// Diálogo de selección de la carpeta de instalación. Vacía si se cancela.
std::filesystem::path askForInstallDirectory();

// Aviso al usuario. Si no hay diálogos disponibles, escribe por stderr.
void showMessage(const std::string& title, const std::string& message, bool error);

// Cuando el launcher se abre con doble clic no hay terminal donde mostrar
// errores ni el instalador de texto. Esto vuelve a lanzarlo dentro de una,
// devolviendo true si lo consiguió. En Windows la consola se reserva en el
// propio proceso, así que no hace falta relanzar nada y devuelve false.
bool respawnInTerminal();

// Entra en dataRoot y sustituye el proceso actual por el juego. No retorna
// si tiene éxito; si falla, escribe el motivo y termina el proceso.
[[noreturn]] void launchGame(const std::filesystem::path& dataRoot,
                             const std::filesystem::path& gameBinary);

} // namespace platform
} // namespace launcher
} // namespace pikmin

#endif // PIKMIN_LAUNCHER_PLATFORM_H
