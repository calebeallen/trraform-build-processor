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

#include "chunk/types/d_chunk.hpp"
#include "config/config.hpp"
#include "utils/build_image.hpp"
#include "utils/plot.hpp"
#include "utils/cf_async_client.hpp"

namespace asio = boost::asio;

asio::awaitable<void> DChunk::downloadPlotUpdates(const std::shared_ptr<const CFAsyncClient> cfCli) {

    // pull updates
    std::vector<GetOutcome> updates;
    {
        std::vector<GetParams> requests;
        requests.reserve(_needsUpdate.size());
        for(size_t i = 0; i < _needsUpdate.size(); ++i)
            requests.push_back({
                VARS::CF_PLOTS_BUCKET,
                fmt::format("{:x}.dat", _needsUpdate[i]),
                _updateFlags[i].metadataOnly
            });

        updates = co_await cfCli->getManyR2Objects(std::move(requests));
    }
     
    // set new plot data
    for (size_t i = 0; i < _needsUpdate.size(); ++i) {
        const std::uint64_t plotId = _needsUpdate[i];
        const auto& flags = _updateFlags[i];
        const auto& obj = updates[i];

        // file must exist here
        if (obj.err)
            throw std::runtime_error(obj.errMsg);

        nlohmann::json json;
        std::span<const std::uint8_t> buildPart;

        if (flags.setDefaultJson) 
            json = Plot::getDefaultJsonPart();
        else if (!flags.metadataOnly)
            json = Plot::getJsonPart(obj.body);
        
        if (flags.setDefaultBuild)
            buildPart = Plot::getDefaultBuildData();
        else if (!flags.metadataOnly)
            buildPart = Plot::getBuildData(obj.body);
        
        const auto itv = obj.metadata.find("verified");
        if (itv == obj.metadata.end())
            throw std::runtime_error("Plot missing verified metadata");
        const auto ito = obj.metadata.find("owner");
        if (ito == obj.metadata.end())
            throw std::runtime_error("Plot missing owner metadata");

        bool verified = itv->second == "true";
        json["verified"] = verified;
        json["owner"] = ito->second;

        // remove subscriber features if needed
        if (!verified) {
            json["link"] = "";
            json["linkTitle"] = "";

            // if build data is greater than max size, remove build
            std::cout << Plot::getBuildSize(obj.body) << std::endl;
            if (Plot::getBuildSize(obj.body) > VARS::BUILD_SIZE_STD)
                buildPart = Plot::getDefaultBuildData();
        }

        // repack plot data
        _parts[plotId] = Plot::makePlotData(json, buildPart);
    }
}

asio::awaitable<void> DChunk::uploadImages(const std::shared_ptr<const CFAsyncClient> cfCli) const {

    std::vector<PutParams> requests; 

    for (size_t i = 0; i < _needsUpdate.size(); ++i) {
        const auto& imgData = _updatedImages[i];
        if (!imgData) 
            continue;

        requests.push_back({
            VARS::CF_IMAGES_BUCKET,
            fmt::format("{:x}.png", _needsUpdate[i]),
            "image/png",
            std::move(*imgData)
        });
    }

    const auto results = co_await cfCli->putManyR2Objects(requests);
    for (const auto& res : results)
        if (res.err)
            throw std::runtime_error(res.errMsg);

}

asio::awaitable<void> DChunk::prep(const std::shared_ptr<const CFAsyncClient> cfCli) {
    co_await downloadParts(cfCli);
    co_await downloadPlotUpdates(cfCli);
}

void DChunk::process() {

    // create new images for plots that need update
    _updatedImages.reserve(_needsUpdate.size());
    for (size_t i = 0; i < _needsUpdate.size(); ++i) {
        const auto plotId = _needsUpdate[i];

        if (_updateFlags[i].noImageUpdate)
            _updatedImages.emplace_back(std::nullopt);
        else {
            const auto buildData = Plot::getBuildPart(_parts[plotId]);
            _updatedImages.emplace_back(BuildImage::make(buildData));
        }
    }

}

asio::awaitable<std::optional<std::string>> DChunk::update(const std::shared_ptr<const CFAsyncClient> cfCli) {
    co_await uploadParts(cfCli);
    co_await uploadImages(cfCli);
    co_return std::nullopt;
}