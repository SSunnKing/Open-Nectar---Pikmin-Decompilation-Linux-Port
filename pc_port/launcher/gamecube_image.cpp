#include "gamecube_image.h"
#include "sha256.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <vector>

namespace fs = std::filesystem;

namespace pikmin {
namespace launcher {
namespace {

constexpr std::uint64_t kFstOffsetField = 0x424;
constexpr std::uint64_t kFstSizeField   = 0x428;
constexpr std::size_t kCopyBufferSize   = 1024 * 1024;

std::uint32_t readBe32(const std::uint8_t* bytes)
{
    return (std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16)
         | (std::uint32_t(bytes[2]) << 8) | std::uint32_t(bytes[3]);
}

bool readAt(std::ifstream& input, std::uint64_t offset, void* output, std::size_t size)
{
    if (offset > std::uint64_t(std::numeric_limits<std::streamoff>::max())) return false;
    input.clear();
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input) return false;
    input.read(static_cast<char*>(output), static_cast<std::streamsize>(size));
    return input.good() || input.gcount() == static_cast<std::streamsize>(size);
}

bool safeComponent(const std::string& name)
{
    if (name.empty() || name == "." || name == "..") return false;
    return std::none_of(name.begin(), name.end(), [](unsigned char c) {
        return c < 0x20 || c == '/' || c == '\\' || c == ':';
    });
}

struct FstEntry {
    bool directory = false;
    std::uint32_t nameOffset = 0;
    std::uint32_t offsetOrParent = 0;
    std::uint32_t sizeOrNext = 0;
};

bool parseFst(std::ifstream& input, std::uint64_t imageSize, std::uint64_t& fstOffset,
              std::vector<FstEntry>& entries, std::vector<std::uint8_t>& fst,
              std::string& error)
{
    std::array<std::uint8_t, 8> header {};
    if (!readAt(input, kFstOffsetField, header.data(), header.size())) {
        error = "No se pudo leer la cabecera FST de la imagen.";
        return false;
    }
    fstOffset = readBe32(header.data());
    const std::uint64_t fstSize = readBe32(header.data() + 4);
    if (fstSize < 12 || fstOffset > imageSize || fstSize > imageSize - fstOffset
        || fstSize > 256ULL * 1024ULL * 1024ULL) {
        error = "La tabla de archivos FST no es válida.";
        return false;
    }
    fst.resize(static_cast<std::size_t>(fstSize));
    if (!readAt(input, fstOffset, fst.data(), fst.size())) {
        error = "No se pudo leer la tabla de archivos FST.";
        return false;
    }
    const std::uint32_t rootWord = readBe32(fst.data());
    const std::uint32_t entryCount = readBe32(fst.data() + 8);
    if ((rootWord >> 24) != 1 || entryCount == 0
        || std::uint64_t(entryCount) * 12ULL > fst.size()) {
        error = "La raíz FST de la imagen no es válida.";
        return false;
    }
    entries.resize(entryCount);
    for (std::uint32_t i = 0; i < entryCount; ++i) {
        const std::uint8_t* raw = fst.data() + std::size_t(i) * 12;
        const std::uint32_t typeAndName = readBe32(raw);
        entries[i].directory = (typeAndName >> 24) != 0;
        entries[i].nameOffset = typeAndName & 0x00FFFFFF;
        entries[i].offsetOrParent = readBe32(raw + 4);
        entries[i].sizeOrNext = readBe32(raw + 8);
        if (entries[i].directory && (entries[i].sizeOrNext <= i
                                     || entries[i].sizeOrNext > entryCount)) {
            error = "La jerarquía FST contiene un directorio inválido.";
            return false;
        }
    }
    return true;
}

bool entryName(const std::vector<std::uint8_t>& fst, std::size_t stringTable,
               const FstEntry& entry, std::string& name)
{
    const std::size_t begin = stringTable + entry.nameOffset;
    if (begin >= fst.size()) return false;
    const auto first = fst.begin() + static_cast<std::ptrdiff_t>(begin);
    const auto end = std::find(first, fst.end(), std::uint8_t(0));
    if (end == fst.end()) return false;
    name.assign(first, end);
    return safeComponent(name);
}

} // namespace

bool inspectGameCubeImage(const fs::path& image, DiscIdentity& identity, std::string& error)
{
    std::ifstream input(image, std::ios::binary);
    if (!input) {
        error = "No se pudo abrir la imagen seleccionada.";
        return false;
    }
    std::array<std::uint8_t, 8> header {};
    if (!readAt(input, 0, header.data(), header.size())) {
        error = "La imagen es demasiado pequeña para ser un disco de GameCube.";
        return false;
    }
    identity.gameId.assign(reinterpret_cast<const char*>(header.data()), 6);
    identity.revision = header[7];
    return true;
}

bool isSupportedPikminDisc(const DiscIdentity& identity, std::string& error)
{
    if (identity.gameId != "GPIE01" || identity.revision != 1) {
        error = "Esta versión inicial del port requiere Pikmin USA Rev. 1 "
                "(GPIE01, revisión 1). Disco detectado: " + identity.gameId
              + ", revisión " + std::to_string(identity.revision) + ".";
        return false;
    }
    return true;
}

namespace {

// Volcado de referencia de Pikmin USA Rev. 1 (GPIE01, revisión 1), disco
// completo de 1.459.978.240 bytes. Una imagen íntegra de ese juego produce
// siempre este hash; cualquier diferencia significa que la copia está dañada,
// recortada o modificada.
constexpr const char* kPikminUsaRev1Sha256 =
    "db013398ec77299e307ef61ec33e82b07e4b21cb676b18a0be712fe55e9775f2";

} // namespace

bool hashImage(const fs::path& image, std::string& hexDigest, std::string& error,
               const std::function<void(std::uint32_t)>& progress)
{
    std::ifstream input(image, std::ios::binary | std::ios::ate);
    if (!input) {
        error = "No se pudo abrir la imagen seleccionada.";
        return false;
    }
    const std::uint64_t imageSize = static_cast<std::uint64_t>(input.tellg());
    input.seekg(0, std::ios::beg);

    Sha256 hash;
    std::vector<char> buffer(kCopyBufferSize);
    std::uint64_t done = 0;
    std::uint32_t lastPercent = 101; // fuerza el primer aviso

    while (done < imageSize) {
        const std::size_t chunk = static_cast<std::size_t>(
            std::min<std::uint64_t>(imageSize - done, buffer.size()));
        input.read(buffer.data(), static_cast<std::streamsize>(chunk));
        if (input.gcount() != static_cast<std::streamsize>(chunk)) {
            error = "No se pudo leer la imagen completa: puede estar dañada o "
                    "el medio de almacenamiento da errores de lectura.";
            return false;
        }
        hash.update(buffer.data(), chunk);
        done += chunk;

        if (progress) {
            const std::uint32_t percent = imageSize == 0
                ? 100u : static_cast<std::uint32_t>(done * 100ULL / imageSize);
            if (percent != lastPercent) {
                progress(percent);
                lastPercent = percent;
            }
        }
    }

    hexDigest = toHex(hash.finish());
    return true;
}

bool verifyImageIntegrity(const fs::path& image, std::string& error,
                          const std::function<void(std::uint32_t)>& progress)
{
    std::string digest;
    if (!hashImage(image, digest, error, progress)) return false;
    if (digest == kPikminUsaRev1Sha256) return true;

    error = "La imagen no coincide con un volcado íntegro de Pikmin USA Rev. 1.\n"
            "Esperado: " + std::string(kPikminUsaRev1Sha256) + "\n"
            "Obtenido: " + digest + "\n\n"
            "Lo más probable es que la copia se haya dañado al transferirla. "
            "Vuelve a copiar el archivo desde el original y comprueba el hash "
            "antes de instalar. Si estás seguro de que tu volcado es correcto y "
            "solo difiere del de referencia, puedes omitir esta comprobación con "
            "--skip-verify.";
    return false;
}

bool extractGameCubeImage(const fs::path& image, const fs::path& destination,
                          std::string& error, ProgressCallback progress,
                          bool verifyWrites)
{
    std::ifstream input(image, std::ios::binary | std::ios::ate);
    if (!input) {
        error = "No se pudo abrir la imagen seleccionada.";
        return false;
    }
    const std::streamoff endPos = input.tellg();
    if (endPos <= 0) {
        error = "La imagen seleccionada está vacía.";
        return false;
    }
    const std::uint64_t imageSize = static_cast<std::uint64_t>(endPos);

    std::uint64_t fstOffset = 0;
    std::vector<FstEntry> entries;
    std::vector<std::uint8_t> fst;
    if (!parseFst(input, imageSize, fstOffset, entries, fst, error)) return false;
    const std::size_t stringTable = entries.size() * 12;

    std::error_code ec;
    fs::create_directories(destination, ec);
    if (ec) {
        error = "No se pudo crear el directorio de assets: " + ec.message();
        return false;
    }

    struct DirectoryFrame { std::uint32_t nextIndex; fs::path path; };
    std::vector<DirectoryFrame> stack;
    stack.push_back({ entries[0].sizeOrNext, destination });
    std::vector<char> buffer(kCopyBufferSize);

    for (std::uint32_t i = 1; i < entries.size(); ++i) {
        while (stack.size() > 1 && i >= stack.back().nextIndex) stack.pop_back();
        std::string name;
        if (!entryName(fst, stringTable, entries[i], name)) {
            error = "La FST contiene un nombre de archivo inseguro o inválido.";
            return false;
        }
        const fs::path outputPath = stack.back().path / name;
        if (progress) progress(i, static_cast<std::uint32_t>(entries.size() - 1),
                               outputPath.lexically_relative(destination).string());

        if (entries[i].directory) {
            fs::create_directories(outputPath, ec);
            if (ec) {
                error = "No se pudo crear " + outputPath.string() + ": " + ec.message();
                return false;
            }
            stack.push_back({ entries[i].sizeOrNext, outputPath });
            continue;
        }

        const std::uint64_t fileOffset = entries[i].offsetOrParent;
        const std::uint64_t fileSize = entries[i].sizeOrNext;
        if (fileOffset > imageSize || fileSize > imageSize - fileOffset) {
            error = "La FST referencia datos fuera de la imagen: " + name;
            return false;
        }
        fs::create_directories(outputPath.parent_path(), ec);
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "No se pudo escribir " + outputPath.string() + ".";
            return false;
        }
        input.clear();
        input.seekg(static_cast<std::streamoff>(fileOffset), std::ios::beg);
        std::uint64_t remaining = fileSize;
        Sha256 sourceHash;
        while (remaining != 0) {
            const std::size_t chunk = static_cast<std::size_t>(
                std::min<std::uint64_t>(remaining, buffer.size()));
            input.read(buffer.data(), static_cast<std::streamsize>(chunk));
            if (input.gcount() != static_cast<std::streamsize>(chunk)) {
                error = "Lectura incompleta al extraer " + name + ".";
                return false;
            }
            sourceHash.update(buffer.data(), chunk);
            output.write(buffer.data(), static_cast<std::streamsize>(chunk));
            if (!output) {
                error = "Escritura incompleta al extraer " + name + ".";
                return false;
            }
            remaining -= chunk;
        }

        // Cerrar de forma explícita: el destructor no informa de los errores de
        // vaciado, y un disco lleno se manifiesta justo aquí.
        output.close();
        if (!output) {
            error = "No se pudo terminar de escribir " + name
                  + ": comprueba el espacio libre en el destino.";
            return false;
        }

        // Releer lo escrito y comparar con lo que salió de la imagen. Detecta
        // daños del medio de destino, que producen archivos del tamaño correcto
        // con contenido incorrecto y hacen fallar al juego mucho más tarde.
        if (verifyWrites) {
            std::ifstream written(outputPath, std::ios::binary);
            if (!written) {
                error = "No se pudo releer " + name + " para verificarlo.";
                return false;
            }
            Sha256 writtenHash;
            std::uint64_t verified = 0;
            while (verified < fileSize) {
                const std::size_t chunk = static_cast<std::size_t>(
                    std::min<std::uint64_t>(fileSize - verified, buffer.size()));
                written.read(buffer.data(), static_cast<std::streamsize>(chunk));
                if (written.gcount() != static_cast<std::streamsize>(chunk)) {
                    error = "No se pudo releer " + name + " completo para verificarlo.";
                    return false;
                }
                writtenHash.update(buffer.data(), chunk);
                verified += chunk;
            }
            if (writtenHash.finish() != sourceHash.finish()) {
                error = "El archivo " + name + " se escribió de forma incorrecta.\n\n"
                        "Los datos del disco de destino no coinciden con los de la "
                        "imagen. Suele indicar un problema del medio de "
                        "almacenamiento (memoria USB defectuosa, disco con "
                        "errores) o falta de espacio. Prueba a instalar en otra "
                        "unidad.";
                return false;
            }
        }
    }
    return true;
}

} // namespace launcher
} // namespace pikmin
