#include <vector>
#include <span>
#include <cstdint>

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

// todo get default build
nlohmann::json Plot::getJsonPart(std::vector<std::uint8_t>& plotData) {

    const size_t jsonLen = getUint32LE(&plotData[0]);

    const char* begin = reinterpret_cast<const char*>(&plotData[4]);
    const char* end = begin + jsonLen;

    return nlohmann::json::parse(begin, end, nullptr, true, false);

}

std::vector<std::uint16_t> Plot::getBuildPart(const std::vector<std::uint8_t>& plotData) {

    const size_t jsonLen = getUint32LE(&plotData[0]);
    const size_t buildLen = getUint32LE(&plotData[jsonLen + 4]);

    // skip to build part
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

std::vector<std::uint8_t> Plot::makeJsonData(const nlohmann::json& fields) {

    nlohmann::json j;
    j["ver"] = 0;
    j["name"] = "";
    j["desc"] = "";
    j["link"] = "";
    j["linkTitle"] = "";
    j["owner"] = "";
    j["verified"] = false;
    j["status"] = "";
    
    for (const auto& [k,v] : fields.items())
        j[k] = v;

    return nlohmann::json::to_cbor(j);

}

std::vector<std::uint8_t> Plot::modifyJsonPart(const std::vector<std::uint8_t>& plotData, const nlohmann::json& updates) {

    // unpack json
    const size_t jsonLen = getUint32LE(&plotData[0]);
    const char* begin = reinterpret_cast<const char*>(&plotData[4]);
    const char* end = begin + jsonLen;
    nlohmann::json json = nlohmann::json::parse(begin, end, nullptr, true, false);

    // update fields
    for (auto& [k,v] : updates.items()) 
        json[k] = v;

    const std::span<const std::uint8_t> buildData(plotData.begin() + jsonLen + 8, plotData.end());
    std::vector<std::uint8_t> jsonVec = nlohmann::json::to_cbor(json);
    const std::span<const std::uint8_t> jsonSpan(jsonVec);
    return Plot::makePlotData(jsonSpan, buildData);

}

std::vector<std::uint8_t> Plot::setBuildPart(const std::vector<std::uint8_t>& plotData, const std::vector<std::uint8_t>& newBuildData) {

    const size_t jsonLen = getUint32LE(&plotData[0]);
    const size_t oldBuildLen = getUint32LE(&plotData[jsonLen + 4]);
    const size_t newBuildLen = newBuildData.size();
    const size_t offset = jsonLen + 8;

    const std::span<const std::uint8_t> jsonDataSpan(plotData.begin() + 4, plotData.begin() + jsonLen + 4);
    const std::span<const std::uint8_t> buildDataSpan(newBuildData);

    return Plot::makePlotData(jsonDataSpan, buildDataSpan);

}
