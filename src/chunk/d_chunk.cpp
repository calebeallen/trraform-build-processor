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
    std::vector<std::string> needsUpdateKeys(_needsUpdate.size());
    for(size_t i = 0; i < _needsUpdate.size(); ++i)
        needsUpdateKeys[i] = fmt::format("{:x}.dat", _needsUpdate[i]);

    auto updates = co_await _cfCli->getManyR2Objects(VARS::CF_PLOTS_BUCKET, needsUpdateKeys);

    // set new plot data
    for (size_t i = 0; i < updates.size(); ++i) {
        const std::uint64_t plotId = _needsUpdate[i];
        const auto& flags = _updateFlags[i];
        auto& out = updates[i];

        // file must exist here
        if (out.err)
            throw std::runtime_error(out.errMsg);

        nlohmann::json json;
        std::span<const std::uint8_t> buildPart;

        /*
        cases:
            standard update: no flags
            subscription created: no flags
            subscription canceled: no flags
            plot removed: default build and default json
        */
        if (flags.setDefaultJson) 
            json = Plot::getDefaultJsonPart();
        else
            json = Plot::getJsonPart(out.body);

        if (flags.setDefaultBuild)
            buildPart = Plot::getDefaultBuildData();
        else
            buildPart = Plot::getBuildData(out.body);
        
        // update metadata fields
        if (out.metadata.find("verified") != out.metadata.end())
            throw std::runtime_error("Plot missing verified metadata");
        if (out.metadata.find("owner") != out.metadata.end())
            throw std::runtime_error("Plot missing owner metadata");

        bool verified = out.metadata["verified"] == "true";
        json["verified"] = verified;
        json["owner"] = out.metadata["owner"];

        // remove subscriber fields
        if (!verified) {
            json["link"] = "";
            json["linkTitle"] = "";
            // if build data is greater than max size, remove build
            if (Plot::getBuildSize(out.body) > VARS::BUILD_SIZE_STD)
                buildPart = Plot::getDefaultBuildData();
        }

        // repack plot data
        _parts[plotId] = Plot::makePlotData(json, buildPart);
    }

}

asio::awaitable<void> DChunk::uploadImages() {

    const auto results = co_await _cfCli->putManyR2Objects(
        VARS::CF_IMAGES_BUCKET,
        _updatedImagesKeys,
        "image/png",
        _updatedImages
    );

    for (const auto& res : results)
        if (res.err)
            throw std::runtime_error(res.errMsg);

}

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
            _updatedImagesKeys.emplace_back(fmt::format("{:X}.png", plotId));
            _updatedImages.emplace_back(BuildImage::make(buildData));
        }
    }

}

asio::awaitable<std::optional<std::string>> DChunk::update() {

    co_await uploadParts();
    co_await uploadImages();
    co_return std::nullopt;

}