#pragma once

#include <unordered_map>

#include <opencv2/core.hpp>

#include "chunk_data.hpp"

class LChunk : virtual ChunkData {

protected:
    std::unordered_map<int,cv::Mat> pointClouds;

public:
    void loadPointClouds();
    void savePointCloud();

};