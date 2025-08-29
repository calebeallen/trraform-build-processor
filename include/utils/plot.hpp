#pragma once

#include <vector>
#include <span>
#include <cstdint>

#include <nlohmann/json.hpp>

namespace Plot {

    nlohmann::json getDefaultJsonPart();
    const std::span<const std::uint8_t> getDefaultBuildData();

    const std::span<const std::uint8_t> getBuildData(const std::vector<uint8_t>&);
    nlohmann::json getJsonPart(std::vector<std::uint8_t>&);
    std::vector<std::uint16_t> getBuildPart(const std::vector<std::uint8_t>&);
    int getBuildSize(const std::vector<std::uint8_t>&);

    std::vector<std::uint8_t> makePlotData(const std::span<const std::uint8_t>&, const std::span<const std::uint8_t>&);

}