#pragma once

#include <vector>
#include <span>
#include <cstdint>

#include <nlohmann/json.hpp>

namespace Plot {

    const std::vector<std::uint8_t>& getDefaultBuildData();
    nlohmann::json getJsonPart(std::vector<std::uint8_t>&);
    std::vector<std::uint16_t> getBuildPart(const std::vector<std::uint8_t>&);
    std::vector<std::uint8_t> getBuildDataU8(const std::vector<std::uint8_t>&);
    int getBuildSize(const std::vector<std::uint8_t>&);

    std::vector<std::uint8_t> makePlotData(const std::span<const std::uint8_t>&, const std::span<const std::uint8_t>&);
    std::vector<std::uint8_t> makePlotData(const nlohmann::json&, const std::span<const std::uint8_t>&);
    std::vector<std::uint8_t> makeJsonData(const nlohmann::json&);
    std::vector<std::uint8_t> modifyJsonPart(const std::vector<std::uint8_t>&, const nlohmann::json&);
    std::vector<std::uint8_t> setBuildPart(const std::vector<std::uint8_t>&, const std::vector<std::uint8_t>&);

}