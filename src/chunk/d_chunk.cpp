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

#include "chunk/d_chunk.hpp"
#include "config/config.hpp"
#include "utils/cf_utils.hpp"
#include "utils/build_image.hpp"
#include "utils/plot.hpp"


DChunk::DChunk(sw::redis::Redis& redisCli, std::string& _chunkId) : ChunkData(_chunkId) {

    // get plot ids that need update
    std::unordered_set<std::string> nu;
    redisCli.smembers(VARS::REDIS_UPDATE_NEEDS_UPDATE_PREFIX + _chunkId, std::inserter(nu, nu.begin()));

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

void DChunk::downloadPlotUpdates() {

    // request all updated plots
    std::vector<Aws::S3::Model::GetObjectOutcomeCallable> futures;
    for (size_t i = 0; i < _needsUpdate.size(); ++i) {
        const std::uint64_t plotId = _needsUpdate[i];

        Aws::S3::Model::GetObjectRequest request;
        request.SetBucket(VARS::CF_PLOTS_BUCKET);
        request.SetKey(plotId + ".dat");
        futures.emplace_back(CFUtils::r2Cli->GetObjectCallable(request), plotId);
    }

    // set new plot data
    for (size_t i = 0; i < _needsUpdate.size(); ++i) {
        const std::uint64_t plotId = _needsUpdate[i];
        const auto& flags = _updateFlags[i];
        auto r = futures[i].get();

        if (!r.IsSuccess()) 
            continue;

        const auto& res = r.GetResultWithOwnership();

        // read metadata
        const auto& metaStream = res.GetMetadata();
        nlohmann::json extJsonFields;
        bool verified;
        if (auto it = metaStream.find("verified"); it != metaStream.end()) {
            verified = it->second == "true";   
            extJsonFields["verified"] = verified;
        }
        if (auto it = metaStream.find("owner"); it != metaStream.end()) {
            extJsonFields["owner"] = it->second.c_str();
        }

        std::vector<std::uint8_t> jsonPart;
        std::vector<std::uint8_t> buildPart;
    
        if (flags.setDefaultPlot) {
            jsonPart = Plot::makeJsonData(extJsonFields);
            buildPart = Plot::getDefaultBuildData();
        } else {
            // request new data if needed
            if (!flags.updateMetadataFieldsOnly || !_parts.contains(plotId)) {
                auto& body = res.GetBody();
                _parts[plotId] = std::vector<uint8_t>{
                    std::istreambuf_iterator<char>(body), 
                    std::istreambuf_iterator<char>()
                };
            }

            // set default build on flag or build size violation
            if (flags.setDefaultBuild || (!verified && Plot::getBuildSize(_parts[plotId]) > VARS::BUILD_SIZE_STD))
                buildPart = Plot::getDefaultBuildData();
            else
                buildPart = Plot::getBuildPart(_parts[plotId]);

            nlohmann::json json = Plot::getJsonPart(_parts[plotId]);
            if (!verified) {
                extJsonFields["link"] = "";
                extJsonFields["linkTitle"] = "";
            }

            // set metadata fields
            _parts[plotId] = std::move(Plot::modifyJsonPart(_parts[plotId], extJsonFields));
            
        }

    }

}

void DChunk::process() {

    // create new images for plots that need update
    for (size_t i = 0; i < _needsUpdate.size(); ++i) {
        const auto plotId = _needsUpdate[i];
        if (!_updateFlags[i].noImageUpdate) {
            const auto buildData = Plot::getBuildPart(_parts[plotId]);
            _updatedJpegs[plotId] = BuildImage::make(buildData);
        }
    }

}

std::optional<std::string> DChunk::update() {
    uploadParts();
}