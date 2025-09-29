#pragma once

#include <span>
#include <unordered_map>
#include <vector>

#include <opencv2/core.hpp>
#include <nlohmann/json.hpp>
#include <boost/asio/awaitable.hpp>

#include "chunk/chunk_data.hpp"
#include "utils/cf_async_client.hpp"

struct UpdateFlags {
    bool metadataOnly = false;
    bool setDefaultJson = false;
    bool setDefaultBuild = false;
    bool noImageUpdate = false;
};

class DChunk : public ChunkData {

protected:
    std::vector<std::optional<std::vector<uint8_t>>> _updatedImages;
    std::vector<UpdateFlags> _updateFlags;
   
    asio::awaitable<void> downloadPlotUpdates();
    asio::awaitable<void> uploadImages();

public:
    DChunk(std::string, std::vector<std::string>, std::vector<UpdateFlags>, std::shared_ptr<CFAsyncClient>);
    virtual ~DChunk() = default;

    virtual asio::awaitable<void> prep() override;
    void process() override;
    virtual asio::awaitable<std::optional<std::string>> update() override;
    
};
