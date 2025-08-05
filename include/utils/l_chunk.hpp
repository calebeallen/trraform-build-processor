#pragma once

#include <unordered_map>

#include <opencv2/core.hpp>

#include "chunk_data.hpp"

class LChunk : public ChunkData {

protected:
    std::unordered_map<int,cv::Mat> _pointClouds;

public:
    void loadPointClouds();
    void savePointCloud();
    void process() override;

};