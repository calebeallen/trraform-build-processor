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

class LChunk : public ChunkData {

private:
    std::unordered_map<std::string, PointCloud> _pointClouds;

public:
    LChunk(std::string, std::vector<std::string>, std::shared_ptr<CFAsyncClient>);

    boost::asio::awaitable<void> prep() override;
    void process() override;
    boost::asio::awaitable<std::optional<std::string>> update() override;

};