#pragma once

#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>

#include <opencv2/core.hpp>
#include <boost/asio/awaitable.hpp>

#include "chunk/chunk_data.hpp"

struct PointCloud {
    cv::Mat points;
    std::vector<uint16_t> colidxs;
};

class LChunk : public virtual ChunkData {

protected:
    std::unordered_map<uint64_t, PointCloud> _pointClouds;

    boost::asio::awaitable<void> downloadPointCloud(const std::shared_ptr<const CFAsyncClient> cfCli);
    boost::asio::awaitable<void> uploadPointCloud(const std::shared_ptr<const CFAsyncClient> cfCli) const;

public:
    LChunk() = default;
    LChunk(
        std::string chunkId, 
        std::vector<std::string> needsUpdate
    ) : ChunkData(std::move(chunkId), std::move(needsUpdate)) {};

    boost::asio::awaitable<void> prep(const std::shared_ptr<const CFAsyncClient> cfCli) override;
    void process() override;
    boost::asio::awaitable<std::optional<std::string>> update(const std::shared_ptr<const CFAsyncClient> cfCli) override;

};