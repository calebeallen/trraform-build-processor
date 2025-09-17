#include <span>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <sstream>

#include <opencv2/core.hpp>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <nlohmann/json.hpp>
#include <boost/asio/awaitable.hpp>
#include <fmt/format.h>

#include "chunk/d_chunk.hpp"
#include "config/config.hpp"
#include "utils/build_image.hpp"
#include "utils/plot.hpp"
#include "utils/cf_async_client.hpp"

namespace asio = boost::asio;

DChunk::DChunk(
    std::string chunkId, 
    std::vector<std::uint64_t> needsUpdate, 
    std::vector<UpdateFlags> updateFlags, 
    std::shared_ptr<CFAsyncClient> cfCli
) : ChunkData(chunkId, std::move(needsUpdate), cfCli) {

    _updateFlags = std::move(updateFlags);

}


asio::awaitable<void> DChunk::downloadPlotUpdates() {

    // pull updates
    std::vector<std::string> needsUpdate(_needsUpdate.size());
    for(size_t i = 0; i < _needsUpdate.size(); ++i)
        needsUpdate[i] = fmt::format("{}", _needsUpdate[i]);

    std::cout << "pulling updates" << std::endl;

    auto updates = co_await _cfCli->getManyR2Objects(VARS::CF_PLOTS_BUCKET, needsUpdate);

    std::cout << updates.size() << std::endl;

    // set new plot data
    for (size_t i = 0; i < updates.size(); ++i) {
        const std::uint64_t plotId = _needsUpdate[i];
        const auto& flags = _updateFlags[i];
        auto& r = updates[i];

        std::cout << "checking success" << std::endl;
        if (!r.IsSuccess()) 
            continue;

        std::cout << "getting with ownership" << std::endl;

        const auto& res = r.GetResultWithOwnership();

        // read metadata
        const auto& metaStream = res.GetMetadata();
        std::cout << "done" << std::endl;
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
        for (const auto& [k,v] : extJsonFields.items())
            json[k] = v;

        // repack plot data
        const std::vector<std::uint8_t> jsonData = nlohmann::json::to_cbor(json);
        _parts[plotId] = Plot::makePlotData(jsonData, buildPart);

    }

}

asio::awaitable<void> DChunk::uploadImages() {

    co_await _cfCli->putManyR2Objects(
        VARS::CF_IMAGES_BUCKET,
        _updatedImagesKeys,
        "image/png",
        _updatedImages
    );

};

asio::awaitable<void> DChunk::prep() {

    co_await downloadParts();
    co_await downloadPlotUpdates();

}

void DChunk::process() {

    // create new images for plots that need update
    for (size_t i = 0; i < _needsUpdate.size(); ++i) {
        const auto plotId = _needsUpdate[i];
        if (!_updateFlags[i].noImageUpdate) {
            const auto buildData = Plot::getBuildPart(_parts[plotId]);
            _updatedImagesKeys.push_back(fmt::format("{:X}.png", plotId));
            _updatedImages.push_back(BuildImage::make(buildData));
        }
    }

}

asio::awaitable<std::optional<std::string>> DChunk::update() {

    co_await uploadParts();
    co_await uploadImages();
    co_return std::nullopt;

}