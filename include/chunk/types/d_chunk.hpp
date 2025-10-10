#pragma once

#include <span>
#include <unordered_map>
#include <vector>

#include <opencv2/core.hpp>
#include <nlohmann/json.hpp>
#include <boost/asio/awaitable.hpp>

#include "chunk/chunk_data.hpp"
#include "utils/cf_async_client.hpp"
#include "utils/plot.hpp"

class DChunk : public virtual ChunkData {

protected:
    std::vector<std::optional<std::vector<uint8_t>>> _updatedImages;
    std::vector<Plot::UpdateFlags> _updateFlags;
   
    asio::awaitable<void> downloadPlotUpdates(const std::shared_ptr<const CFAsyncClient> cfCli);
    asio::awaitable<void> uploadImages(const std::shared_ptr<const CFAsyncClient> cfCli) const;

public:
    DChunk(std::vector<Plot::UpdateFlags> updateFlags) : _updateFlags(std::move(updateFlags)) {};
    DChunk(
        std::string chunkId, 
        std::vector<std::string> needsUpdate, 
        std::vector<Plot::UpdateFlags> updateFlags
    ) : ChunkData(std::move(chunkId), std::move(needsUpdate)),
    _updateFlags(std::move(updateFlags)) {};

    virtual ~DChunk() = default;

    virtual asio::awaitable<void> prep(const std::shared_ptr<const CFAsyncClient> cfCli) override;
    void process() override;
    virtual asio::awaitable<std::optional<std::string>> update(const std::shared_ptr<const CFAsyncClient> cfCli) override;
    
};
