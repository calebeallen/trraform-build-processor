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


DChunk::DChunk(std::string chunkId, std::vector<std::uint64_t> needsUpdate, std::vector<UpdateFlags> updateFlags) : ChunkData(chunkId, std::move(needsUpdate)) {

    _updateFlags = std::move(updateFlags);

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
        
        nlohmann::json json;
        std::span<const std::uint8_t> buildPart;
    
        if (flags.setDefaultPlot) {
            json = Plot::getDefaultJsonPart();
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

            // extract json part
            json = Plot::getJsonPart(_parts[plotId]);
            if (!verified) {
                json["link"] = "";
                json["linkTitle"] = "";
            }

            // set default build on flag or build size violation
            if (flags.setDefaultBuild || (!verified && Plot::getBuildSize(_parts[plotId]) > VARS::BUILD_SIZE_STD))
                buildPart = Plot::getDefaultBuildData();
            else
                buildPart = Plot::getBuildData(_parts[plotId]);
            
        }

        // set external fields
        for (const auto [k,v] : extJsonFields.items())
            json[k] = v;

        // repack plot data
        const std::vector<std::uint8_t> jsonData = nlohmann::json::to_cbor(json);
        _parts[plotId] = Plot::makePlotData(jsonData, buildPart);

    }

}

void DChunk::prep() {

    downloadParts();
    downloadPlotUpdates();

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