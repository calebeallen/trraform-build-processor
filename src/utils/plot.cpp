#include <vector>
#include <span>
#include <cstdint>
#include <fstream>    
#include <stdexcept>
#include <iterator>    
#include <iostream>

#include <nlohmann/json.hpp>

#include "utils/plot.hpp"


nlohmann::json Plot::getDefaultJsonPart() {
    nlohmann::json j;
    j["ver"] = 0;
    j["name"] = "";
    j["desc"] = "";
    j["link"] = "";
    j["linkTitle"] = "";
    j["owner"] = "";
    j["verified"] = false;
    j["status"] = "";
    return j;
}

std::span<const uint8_t> Plot::getDefaultBuildData() {
    static const auto defaultBuild = []() {
        std::ifstream file("static/default_cactus.dat", std::ios::binary);
    
        assert(file && "Failed to open default_cactus.dat");

        std::vector<uint8_t> data(
            (std::istreambuf_iterator<char>(file)),
            (std::istreambuf_iterator<char>())
        );
        return data;
    }();

    return { defaultBuild.data(), defaultBuild.size() };
}

std::span<const uint8_t> Plot::getBuildData(const std::vector<uint8_t>& plotData) {
    uint32_t jsonLen;
    std::memcpy(&jsonLen, plotData.data(), sizeof(uint32_t));
    const size_t offset = static_cast<size_t>(jsonLen) + 8;

    return { plotData.data() + offset, plotData.size() - offset };
}

nlohmann::json Plot::getJsonPart(const std::vector<uint8_t>& plotData) {
    uint32_t jsonLen;
    std::memcpy(&jsonLen, plotData.data(), sizeof(uint32_t));

    const char* begin = reinterpret_cast<const char*>(&plotData[4]);
    const char* end = begin + jsonLen;

    return nlohmann::json::parse(begin, end, nullptr, true, false);
}

std::vector<std::uint16_t> Plot::getBuildPart(const std::vector<std::uint8_t>& plotData) {
    std::uint32_t jsonLen;
    std::uint32_t buildLen;

    std::memcpy(&jsonLen, plotData.data(), sizeof(std::uint32_t));
    std::memcpy(&buildLen, plotData.data() + jsonLen + 4, sizeof(std::uint32_t));

    // copy to avoid misalignment
    std::vector<std::uint16_t> buildData(buildLen / 2);
    std::memcpy(&buildData[0], &plotData[jsonLen + 8], buildLen);

    return buildData;
}

std::uint16_t Plot::getBuildSize(const std::vector<std::uint8_t>& plotData) {
    std::uint32_t jsonLen;
    std::uint16_t buildSize;

    std::memcpy(&jsonLen, plotData.data(), sizeof(std::uint32_t));
    std::memcpy(&buildSize, plotData.data() + jsonLen + 10, sizeof(std::uint16_t));
    
    // skip to build part
    return buildSize;
}

std::vector<std::uint8_t> Plot::makePlotData(const nlohmann::json& json, const std::span<const uint8_t>& buildData) {
     
    const std::string jsonData = json.dump();
    const std::uint32_t jsonLen = jsonData.size();
    const std::uint32_t buildLen = buildData.size();
    std::vector<std::uint8_t> plotData(jsonLen + buildLen + 8);
   
    // set len prefixes
    std::memcpy(plotData.data(), &jsonLen, sizeof(std::uint32_t));
    std::memcpy(plotData.data() + jsonLen + 4, &buildLen, sizeof(std::uint32_t));

    // set data
    std::memcpy(plotData.data() + 4, jsonData.data(), jsonLen);
    std::memcpy(plotData.data() + jsonLen + 8, buildData.data(), buildLen);
  
    return plotData;
}
