#pragma once

#include <span>
#include <unordered_map>
#include <vector>

#include <opencv2/opencv.hpp>

#include "chunk_data.hpp"

class DChunk : public ChunkData {

public:
    std::unordered_map<std::string,std::vector<uchar>> _updatedJpegs;

    // plot helpers (maybe move to sep namespace?)
    static const std::span<uint16_t> getBuildData(std::vector<uint8_t>&);
    static cv::Mat idxToPos(const int, const int);
    static int getLocPlotId(const int);
    static int getLocPlotId(const std::string&);

    void pullPlotUpdates();
    std::vector<uint8_t> makeBuildImage();

    virtual void process() override;
    
};
