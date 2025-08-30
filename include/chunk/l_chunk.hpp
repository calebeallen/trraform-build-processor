#pragma once

#include <unordered_map>
#include <string>

#include <opencv2/core.hpp>

#include "chunk/chunk_data.hpp"

class LChunk : public ChunkData {

private:
    std::unordered_map<int,cv::Mat> _pointClouds;

public:
    LChunk(std::string, std::vector<std::uint64_t>);

    void prep() override;
    void process() override;
    std::optional<std::string> update() override;

};