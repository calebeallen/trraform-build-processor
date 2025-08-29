#pragma once

#include <unordered_map>
#include <string>

#include <opencv2/core.hpp>

#include "chunk/chunk_data.hpp"

class LChunk : public ChunkData {

protected:
    std::unordered_map<int,cv::Mat> _pointClouds;

    void loadPointClouds();
    void savePointCloud();

public:
    LChunk(const std::string&);

    void process() override;
    std::optional<std::string> update() override;

};