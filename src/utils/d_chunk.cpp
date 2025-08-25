#include <span>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <sstream>

#include <opencv2/core.hpp>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <sw/redis++/redis++.h>
#include <nlohmann/json.hpp>

#include "d_chunk.hpp"
#include "constants.hpp"
#include "cf_util.hpp"
#include "build_image.hpp"

const std::span<uint16_t> DChunk::getBuildData(std::vector<uint8_t>& plotData){

    // skip json part
    size_t i = static_cast<size_t>(plotData[0]) |
        (static_cast<size_t>(plotData[1]) << 8) |
        (static_cast<size_t>(plotData[2]) << 16) |
        (static_cast<size_t>(plotData[3]) << 24);
    i += 4;

    size_t buildLen = static_cast<size_t>(plotData[i]) |
        (static_cast<size_t>(plotData[i+1]) << 8) |
        (static_cast<size_t>(plotData[i+2]) << 16) |
        (static_cast<size_t>(plotData[i+3]) << 24);
    i += 4;

    uint8_t* u8 = plotData.data() + i;
    uint16_t* u16 = reinterpret_cast<uint16_t*>(u8);

    return std::span<uint16_t>(u16, buildLen / 2);
 
}

int DChunk::getBuildSize(const std::vector<uint8_t>& plotData) {

    // skip json part + size headers
    size_t i = static_cast<size_t>(plotData[0]) |
        (static_cast<size_t>(plotData[1]) << 8) |
        (static_cast<size_t>(plotData[2]) << 16) |
        (static_cast<size_t>(plotData[3]) << 24);
    i += 8;

    // if for some reason no build data
    if (i == plotData.size() - 1)
        return 0;

    // skip first unused val
    i += 2;

    return static_cast<int>(plotData[i]) | (static_cast<int>(plotData[i+1]) << 8);

}

void DChunk::setPlotDataBuild(std::vector<std::uint8_t>& plotData, const std::vector<uint8_t>& newBuildData) {

    // skip json part
    size_t offset = static_cast<size_t>(plotData[0]) |
        (static_cast<size_t>(plotData[1]) << 8) |
        (static_cast<size_t>(plotData[2]) << 16) |
        (static_cast<size_t>(plotData[3]) << 24);
    offset += 4;

    const size_t oldBuildLen = static_cast<size_t>(plotData[offset]) |
        (static_cast<size_t>(plotData[offset + 1]) << 8) |
        (static_cast<size_t>(plotData[offset + 2]) << 16) |
        (static_cast<size_t>(plotData[offset + 3]) << 24);

    const size_t newBuildLen = newBuildData.size();
    plotData[offset] = static_cast<size_t>(newBuildLen);
    plotData[offset + 1] = static_cast<size_t>(newBuildLen >> 8);
    plotData[offset + 2] = static_cast<size_t>(newBuildLen >> 16);
    plotData[offset + 3] = static_cast<size_t>(newBuildLen >> 24);
    offset += 4;

    if (newBuildLen > oldBuildLen) {
        const size_t delta = newBuildLen - oldBuildLen;
        plotData.insert(plotData.begin() + offset, delta, 0);
    } else if (newBuildLen < oldBuildLen) {
        const size_t delta = oldBuildLen - newBuildLen;
        plotData.erase(plotData.begin() + offset + newBuildLen, plotData.begin() + offset + oldBuildLen);
    }
    std::memcpy(&plotData[offset], newBuildData.data(), newBuildLen);

}

void DChunk::setPlotDataJson(std::vector<std::uint8_t>& plotData, const nlohmann::json& fields) {

    // read json length
    size_t jsonLen =  static_cast<size_t>(plotData[0]) |
        (static_cast<size_t>(plotData[1]) << 8) |
        (static_cast<size_t>(plotData[2]) << 16) |
        (static_cast<size_t>(plotData[3]) << 24);

    // get json str
    std::string jsonStr(reinterpret_cast<char*>(&plotData[4]), jsonLen);

    // unpack json
    nlohmann::json json = nlohmann::json::parse(jsonStr, nullptr, /*allow_exceptions=*/true);

    // update fields
    for (auto& [k,v] : fields.items()) 
        json[k] = v;

    // repack json
    std::string newJson = json.dump();
    const size_t newLen = newJson.size();

    // resize plot data
    const size_t tailOffOld = jsonLen + 4;
    if (newLen > jsonLen) {
        plotData.insert(plotData.begin() + tailOffOld, newLen - jsonLen, 0);
    } else if (jsonLen > newLen) {
        plotData.erase(plotData.begin() + 4 + newLen, plotData.begin() + 4 + jsonLen);
    }

    // write new json length
    plotData[0] = static_cast<uint8_t>(newLen);
    plotData[1] = static_cast<uint8_t>(newLen >> 8);
    plotData[2] = static_cast<uint8_t>(newLen >> 16);
    plotData[3] = static_cast<uint8_t>(newLen >> 24);

    // mem copy new data
    std::memcpy(&plotData[4], newJson.data(), newLen);

}

int DChunk::getLocPlotId(const int plotId) {
    return plotId <= VARS::PLOT_COUNT ? 0 : plotId & 0xFFF;
}

int DChunk::getLocPlotId(const std::string& plotIdStr) {
    return getLocPlotId(stoi(plotIdStr, nullptr, 16));
}

DChunk::DChunk(sw::redis::Redis& redisCli, std::string& _chunkId) : ChunkData(_chunkId) {

    // get plot ids that need update
    std::unordered_set<std::string> nu;
    redisCli.smembers(VARS::REDIS_UPDATE_NEEDS_UPDATE_FLAGS_PREFIX + _chunkId, std::inserter(nu, nu.begin()));

    std::vector<std::string> getFlagsKeys;
    for (const auto &plotId : nu) {
        getFlagsKeys.push_back(VARS::REDIS_UPDATE_NEEDS_UPDATE_FLAGS_PREFIX + plotId);
        _needsUpdate.push_back(stoll(plotId, nullptr, 16));
    }

    // get update flags for plot ids that need update
    std::vector<std::optional<std::string>> flags;
    redisCli.mget(getFlagsKeys.begin(), getFlagsKeys.end(), std::back_inserter(flags));
    _updateFlags.resize(_needsUpdate.size());

    for (size_t i = 0; i < _updateFlags.size(); ++i) {
        if (flags[i]) {
            std::stringstream ss(*flags[i]);
            std::string token;
            
            while (std::getline(ss, token, ' ')) {
                if (token == VARS::REDIS_NO_IMAGE_UPDATE_FLAG)
                    _updateFlags[i].noImageUpdate = true;
                else if (token == VARS::REDIS_SET_DEFAULT_BUILD_FLAG)
                    _updateFlags[i].setDefaultBuild = true;
            }
        }
    }

    // pull updates after parsing needs update + flags
    downloadPlotUpdates();

}

void DChunk::downloadPlotUpdates(){

    typedef struct{
        std::uint64_t id;
        Aws::S3::Model::GetObjectOutcomeCallable req;
    } UpdateReq;

    // request all updated plots
    std::vector<UpdateReq> futures;
    for (size_t i = 0; i < _needsUpdate.size(); ++i) {
        auto& plotId = _needsUpdate[i];

        // skip request if setting default build
        if (_updateFlags[i].setDefaultBuild) {
            setPlotDataBuild(_parts[plotId], getDefaultBuildData());
        } else {
            Aws::S3::Model::GetObjectRequest request;
            request.SetBucket(VARS::CF_PLOTS_BUCKET);
            request.SetKey(plotId + ".dat");
            futures.emplace_back(r2Cli->GetObjectCallable(request), plotId);
        }

    }

    // set new plot data
    for (auto& f : futures) {
        auto r = f.req.get();
        if (!r.IsSuccess()) 
            continue;

        const auto& res = r.GetResultWithOwnership();

        // read metadata
        const auto& metaStream = res.GetMetadata();
        nlohmann::json metadata;
        bool verified;
        if (auto it = metaStream.find("verified"); it != metaStream.end()) {
            verified = it->second.c_str() == "true";   
            metadata["verified"] = verified;
        }
        if (auto it = metaStream.find("owner"); it != metaStream.end()) {
            metadata["owner"] = it->second.c_str();
        }

        // read body
        auto& stream = res.GetBody();
        std::vector<uint8_t> data{
            std::istreambuf_iterator<char>(stream), 
            std::istreambuf_iterator<char>()
        };

        // update metadata fields
        setPlotDataJson(data, metadata);

        // if not verified, replace build that exceed standard size limit with default build
        if (!verified && getBuildSize(data) > VARS::BUILD_SIZE_STD) {
            setPlotDataBuild(data, getDefaultBuildData());  
        }
    
        _parts[f.id] = std::move(data);
    }

}

void DChunk::process() {

    // create new images for plots that need update
    for (size_t i = 0; i < _needsUpdate.size(); ++i) {
        const auto plotId = _needsUpdate[i];
        if (!_updateFlags[i].noImageUpdate)
            _updatedJpegs[plotId] = BuildImage::make(getBuildData(_parts[plotId]));
    }

}