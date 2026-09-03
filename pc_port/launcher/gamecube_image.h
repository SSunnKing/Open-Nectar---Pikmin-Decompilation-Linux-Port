#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace pikmin {
namespace launcher {

struct DiscIdentity {
    std::string gameId;
    std::uint8_t revision = 0;
};

using ProgressCallback = std::function<void(std::uint32_t current, std::uint32_t total,
                                            const std::string& path)>;

bool inspectGameCubeImage(const std::filesystem::path& image, DiscIdentity& identity,
                          std::string& error);

bool extractGameCubeImage(const std::filesystem::path& image,
                          const std::filesystem::path& destination,
                          std::string& error, ProgressCallback progress = {});

bool isSupportedPikminDisc(const DiscIdentity& identity, std::string& error);

} // namespace launcher
} // namespace pikmin
