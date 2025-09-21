#pragma once

#include <vector>
#include <span>
#include <cstdint>

#include <nlohmann/json.hpp>

namespace Plot {

    nlohmann::json getDefaultJsonPart();
    std::span<const std::uint8_t> getDefaultBuildData();

    std::span<const std::uint8_t> getBuildData(const std::vector<uint8_t>&);
    nlohmann::json getJsonPart(const std::vector<std::uint8_t>&);
    std::vector<std::uint16_t> getBuildPart(const std::vector<std::uint8_t>&);
    std::uint16_t getBuildSize(const std::vector<std::uint8_t>&);

    std::vector<std::uint8_t> makePlotData(const nlohmann::json&, const std::span<const std::uint8_t>&);

}