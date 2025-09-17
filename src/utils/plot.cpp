#include <vector>
#include <span>
#include <cstdint>
#include <fstream>    
#include <stdexcept>
#include <iterator>    

#include <nlohmann/json.hpp>

#include "utils/plot.hpp"

static size_t getUint32LE(const uint8_t* data) {
    return static_cast<size_t>(data[0]) |
        (static_cast<size_t>(data[1]) << 8) |
        (static_cast<size_t>(data[2]) << 16) |
        (static_cast<size_t>(data[3]) << 24);
}

static void setUint32LE(uint8_t* data, std::uint32_t val) {
    data[0] = static_cast<uint8_t>(val);
    data[1] = static_cast<uint8_t>(val >> 8);
    data[2] = static_cast<uint8_t>(val >> 16);
    data[3] = static_cast<uint8_t>(val >> 24);
}


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


const std::span<const std::uint8_t> Plot::getDefaultBuildData() {

    static const auto defaultBuild = []() {
        std::ifstream file("static/default_build.dat", std::ios::binary);
        if (!file) {
            throw std::runtime_error("Failed to open static/default_build.dat");
        }

        std::vector<std::uint8_t> data(
            (std::istreambuf_iterator<char>(file)),
            (std::istreambuf_iterator<char>())
        );

        return data;
    }();

    return defaultBuild;

}


const std::span<const std::uint8_t> Plot::getBuildData(const std::vector<std::uint8_t>& plotData) {

    const size_t jsonLen = getUint32LE(&plotData[0]);
    return std::span<const std::uint8_t>(plotData.begin() + jsonLen + 8, plotData.end());
 
}

nlohmann::json Plot::getJsonPart(std::vector<std::uint8_t>& plotData) {

    const size_t jsonLen = getUint32LE(&plotData[0]);

    const char* begin = reinterpret_cast<const char*>(&plotData[4]);
    const char* end = begin + jsonLen;

    return nlohmann::json::parse(begin, end, nullptr, true, false);

}

std::vector<std::uint16_t> Plot::getBuildPart(const std::vector<std::uint8_t>& plotData) {

    const size_t jsonLen = getUint32LE(&plotData[0]);
    const size_t buildLen = getUint32LE(&plotData[jsonLen + 4]);

    // copy to avoid misalignment
    std::vector<std::uint16_t> buildData(buildLen / 2);
    std::memcpy(&buildData[0], &plotData[jsonLen + 8], buildLen);

    return buildData;
 
}

int Plot::getBuildSize(const std::vector<std::uint8_t>& plotData) {

    const size_t jsonLen = getUint32LE(&plotData[0]);
    const size_t buildLen = getUint32LE(&plotData[jsonLen + 4]);

    // skip to build part
    const size_t i = jsonLen + 10;

    return static_cast<int>(plotData[i]) | (static_cast<int>(plotData[i+1]) << 8);

}

std::vector<std::uint8_t> Plot::makePlotData(const std::span<const std::uint8_t>& jsonData, const std::span<const std::uint8_t>& buildData) {

    const size_t jsonLen = jsonData.size();
    const size_t buildLen = buildData.size();
    std::vector<std::uint8_t> plotData(jsonLen + buildLen + 8);

    // set len prefixes
    setUint32LE(&plotData[0], jsonLen);
    setUint32LE(&plotData[jsonLen + 4], buildLen);

    // set data
    std::memcpy(&plotData[4], &jsonData[0], jsonLen);
    std::memcpy(&plotData[jsonLen + 8], &buildData[0], buildLen);

    return plotData;

}
