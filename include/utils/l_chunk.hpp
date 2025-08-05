#pragma once

#include <unordered_map>

#include <opencv2/core.hpp>

#include "chunk_data.hpp"

class LChunk : public ChunkData {

protected:
    std::unordered_map<int,cv::Mat> _pointClouds;

    void loadPointClouds();
    void savePointCloud();

public:
    void process() override;
    void update() override;

};