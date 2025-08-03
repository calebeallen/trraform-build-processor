#pragma once

#include <span>
#include <unordered_map>
#include <vector>

#include <opencv2/opencv.hpp>

#include "chunk_data.hpp"

class DChunk : virtual ChunkData {

public:
    std::unordered_map<int,std::vector<uchar>> updatedImages;

    static const std::span<uint8_t> getBuildData(std::vector<uint8_t>&);

    void pullPlotUpdates();
    std::vector<uchar> makeBuildImage();
    
};
