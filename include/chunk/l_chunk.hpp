#pragma once

#include <unordered_map>
#include <string>
#include <cstdint>

#include <opencv2/core.hpp>
#include <boost/asio/awaitable.hpp>

#include "chunk/chunk_data.hpp"

class LChunk : public ChunkData {

private:
    std::unordered_map<uint64_t,cv::Mat> _pointClouds;

public:
    LChunk(std::string, std::vector<uint64_t>, std::shared_ptr<CFAsyncClient>);

    boost::asio::awaitable<void> prep() override;
    void process() override;
    boost::asio::awaitable<std::optional<std::string>> update() override;

};